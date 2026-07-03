#include "executor.h"
#include "parsegraph/graph.h"
#include "../core/graph_internal.h"
#include "../util/error.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_executor_create(pg_executor **out, pg_graph *g,
                              pg_query_plan *plan) {
    pg_executor *exec = calloc(1, sizeof(pg_executor));
    if (!exec) return PG_ERR_NOMEM;

    exec->graph = g;
    exec->plan = plan;
    pg_cursor_init(&exec->cursor, g);
    pg_match_result_init(&exec->result);
    pg_backtrack_init(&exec->backtrack);

    /* snapshot the graph for consistent reads */
    pg_graph_snapshot(g, &exec->snapshot);

    /* set up pipeline stages based on plan ops */
    pg_plan_op *op = plan->root;
    while (op) {
        switch (op->type) {
        case PG_OP_FILTER:
            exec->has_filter = true;
            /* filter predicate comes from AST */
            if (plan->ast && plan->ast->type == PG_AST_MATCH &&
                plan->ast->data.match.where_clause) {
                pg_filter_init(&exec->filter,
                               plan->ast->data.match.where_clause->data.filter.predicate);
            }
            break;
        case PG_OP_PROJECT:
            exec->has_project = true;
            pg_project_init(&exec->project, op->columns, op->column_count);
            break;
        case PG_OP_AGGREGATE:
            exec->has_aggregate = true;
            pg_agg_init(&exec->aggregate, (pg_agg_func)op->agg_func,
                        op->agg_column);
            break;
        case PG_OP_HASH_JOIN:
            exec->has_join = true;
            pg_join_init(&exec->join);
            break;
        default:
            break;
        }
        op = op->child;
    }

    exec->rows_produced = 0;
    *out = exec;
    return PG_OK;
}

void pg_executor_destroy(pg_executor *exec) {
    if (!exec) return;
    pg_cursor_close(&exec->cursor);
    pg_match_result_free(&exec->result);
    pg_backtrack_free(&exec->backtrack);

    /* release snapshot */
    if (exec->snapshot) {
        pg_snapshot_release(exec->snapshot);
        exec->snapshot = NULL;
    }

    if (exec->has_project) pg_project_free(&exec->project);
    if (exec->has_join) pg_join_free(&exec->join);
    free(exec);
}

pg_status pg_executor_run_scan(pg_executor *exec, pg_plan_op *op) {
    (void)op;
    /* full scan: iterate all nodes in the graph via arena */
    pg_arena_segment *seg = exec->graph->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_node *n = (pg_node *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (n->deleted_gen != 0) continue;

            pg_cursor_seek_node(&exec->cursor, n->id);
            pg_match_result_add_node(&exec->result, n);
        }
        seg = seg->next;
    }
    return PG_OK;
}

pg_status pg_executor_run_index_scan(pg_executor *exec, pg_plan_op *op) {
    /* scan by label — delegated to label index in batch handler */
    (void)op;
    /* for now, fall back to full scan with label filter */
    pg_arena_segment *seg = exec->graph->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_node *n = (pg_node *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (n->deleted_gen != 0) continue;
            if (op && op->label != PG_INVALID_LABEL && n->label != op->label)
                continue;

            pg_cursor_seek_node(&exec->cursor, n->id);
            pg_match_result_add_node(&exec->result, n);
        }
        seg = seg->next;
    }
    return PG_OK;
}

pg_status pg_executor_run_filter(pg_executor *exec) {
    if (!exec->has_filter) return PG_OK;

    /* filter in-place: remove non-matching nodes from result */
    uint32_t write = 0;
    for (uint32_t i = 0; i < exec->result.node_count; i++) {
        pg_node *n = exec->result.nodes[i];
        if (!n) continue;
        pg_filter_reset(&exec->filter);
        if (pg_filter_evaluate(&exec->filter, n, exec->graph)) {
            exec->result.nodes[write++] = n;
        }
    }
    exec->result.node_count = write;
    return PG_OK;
}

pg_status pg_executor_run_project(pg_executor *exec) {
    if (!exec->has_project) return PG_OK;
    return pg_project_row(&exec->project, &exec->result);
}

pg_status pg_executor_run_aggregate(pg_executor *exec) {
    if (!exec->has_aggregate) return PG_OK;

    for (uint32_t i = 0; i < exec->result.node_count; i++) {
        pg_node *n = exec->result.nodes[i];
        if (!n) continue;

        /* aggregate over the specified column (treated as property key) */
        pg_value *val = pg_props_get(&n->properties, exec->aggregate.column);
        if (val) {
            /*
             * BUG-5 THROUGH:
             * If filter cached a property value pointer and then a batch
             * mutation freed it via pg_props_set, passing the filter's
             * cached_prop_value to aggregate_accumulate reads freed memory.
             */
            if (exec->has_filter && exec->filter.cached_prop_value) {
                pg_agg_accumulate(&exec->aggregate,
                                   exec->filter.cached_prop_value);
            } else {
                pg_agg_accumulate(&exec->aggregate, val);
            }
        }
    }
    return PG_OK;
}

pg_status pg_executor_run_pattern(pg_executor *exec, pg_ast_node *pattern) {
    if (!pattern) return PG_OK;

    /* save backtrack point for speculative matching */
    if (exec->snapshot) {
        pg_backtrack_save(&exec->backtrack, exec->snapshot,
                          &exec->cursor, 0, &exec->result);
    }

    pg_status s = pg_match_pattern(exec->graph, &exec->cursor,
                                    pattern, &exec->result);

    /* if match failed, try backtracking */
    if (s == PG_ERR_NOT_FOUND && pg_backtrack_has_points(&exec->backtrack)) {
        uint32_t depth;
        pg_backtrack_restore(&exec->backtrack, &exec->cursor,
                              &depth, &exec->result);
    }
    return PG_OK;
}

pg_status pg_executor_run(pg_executor *exec) {
    pg_plan_op *op = exec->plan->root;

    /* execute plan tree depth-first */
    while (op) {
        switch (op->type) {
        case PG_OP_SCAN:
            pg_executor_run_scan(exec, op);
            break;
        case PG_OP_INDEX_SCAN:
            pg_executor_run_index_scan(exec, op);
            break;
        case PG_OP_FILTER:
            pg_executor_run_filter(exec);
            break;
        case PG_OP_PROJECT:
            pg_executor_run_project(exec);
            break;
        case PG_OP_AGGREGATE:
            pg_executor_run_aggregate(exec);
            break;
        case PG_OP_HASH_JOIN:
            /* build from current result, probe from child result */
            if (exec->has_join) {
                pg_join_build(&exec->join, &exec->result);
            }
            break;
        case PG_OP_TRAVERSE:
            /* pattern matching handled via executor_run_pattern */
            if (exec->plan->ast &&
                exec->plan->ast->type == PG_AST_MATCH) {
                pg_executor_run_pattern(exec,
                    exec->plan->ast->data.match.pattern);
            }
            break;
        case PG_OP_EMIT:
            exec->rows_produced = exec->result.node_count;
            break;
        }
        op = op->child;
    }

    return PG_OK;
}

/* ── Extended pipeline execution ─────────────────────────── */

/*
 * Execute a hash join between two scan results.
 * Build phase: populate hash table from left result.
 * Probe phase: iterate right result, look up matches.
 */
pg_status pg_executor_run_hash_join(pg_executor *exec, pg_plan_op *op) {
    if (!exec->has_join) return PG_OK;
    (void)op;

    /* build phase: already done in PG_OP_HASH_JOIN handler above */
    /* probe phase: iterate edges to find matching dst nodes */
    pg_match_result probe_result;
    pg_match_result_init(&probe_result);

    for (uint32_t i = 0; i < exec->result.node_count; i++) {
        pg_node *n = exec->result.nodes[i];
        if (!n) continue;

        for (uint32_t e = 0; e < n->outgoing.count; e++) {
            pg_edge_id eid = n->outgoing.edges[e];
            pg_edge *edge = pg_graph_get_edge(exec->graph, eid);
            if (!edge) continue;

            pg_node *dst = pg_graph_get_node(exec->graph, edge->dst);
            if (!dst) continue;

            /* check if dst is in the build table */
            void *match = pg_map_get(&exec->join.build_table,
                                      (uint64_t)dst->id);
            if (match) {
                pg_match_result_add_node(&probe_result, n);
                pg_match_result_add_node(&probe_result, dst);
                pg_match_result_add_edge(&probe_result, edge);
                exec->join.output_count++;
            }
        }
    }

    /* replace current result with join output */
    if (probe_result.node_count > 0) {
        pg_match_result_free(&exec->result);
        exec->result = probe_result;
    } else {
        pg_match_result_free(&probe_result);
    }

    return PG_OK;
}

/*
 * Execute a traversal: from the current result set, follow edges
 * of the specified type and add destination nodes to the result.
 */
pg_status pg_executor_run_traverse(pg_executor *exec, pg_plan_op *op) {
    pg_match_result traversed;
    pg_match_result_init(&traversed);

    for (uint32_t i = 0; i < exec->result.node_count; i++) {
        pg_node *n = exec->result.nodes[i];
        if (!n) continue;

        pg_cursor_seek_node(&exec->cursor, n->id);
        pg_cursor_follow_edge(&exec->cursor, true);

        pg_edge_id eid;
        while (pg_cursor_next_neighbor(&exec->cursor, &eid)) {
            pg_edge *edge = pg_graph_get_edge(exec->graph, eid);
            if (!edge) continue;

            /* filter by edge type if specified */
            if (op && op->edge_type != PG_INVALID_LABEL &&
                edge->edge_type != op->edge_type) {
                continue;
            }

            pg_node *dst = pg_graph_get_node(exec->graph, edge->dst);
            if (dst && dst->deleted_gen == 0) {
                pg_match_result_add_node(&traversed, dst);
                pg_match_result_add_edge(&traversed, edge);
            }
        }
    }

    /* merge traversed nodes into result */
    for (uint32_t i = 0; i < traversed.node_count; i++) {
        pg_match_result_add_node(&exec->result, traversed.nodes[i]);
    }
    for (uint32_t i = 0; i < traversed.edge_count; i++) {
        pg_match_result_add_edge(&exec->result, traversed.edges[i]);
    }

    pg_match_result_free(&traversed);
    return PG_OK;
}

/*
 * Compute statistics over the current result set.
 */
typedef struct pg_exec_stats {
    uint32_t total_nodes;
    uint32_t total_edges;
    uint32_t nodes_with_props;
    uint32_t total_props;
    uint32_t unique_labels;
    pg_label_id labels_seen[64];
    uint32_t label_count;
} pg_exec_stats;

void pg_executor_compute_stats(pg_executor *exec, pg_exec_stats *stats) {
    memset(stats, 0, sizeof(*stats));
    stats->total_nodes = exec->result.node_count;
    stats->total_edges = exec->result.edge_count;

    for (uint32_t i = 0; i < exec->result.node_count; i++) {
        pg_node *n = exec->result.nodes[i];
        if (!n) continue;

        if (n->properties.count > 0) {
            stats->nodes_with_props++;
            stats->total_props += n->properties.count;
        }

        /* track unique labels */
        bool found = false;
        for (uint32_t j = 0; j < stats->label_count; j++) {
            if (stats->labels_seen[j] == n->label) {
                found = true;
                break;
            }
        }
        if (!found && stats->label_count < 64) {
            stats->labels_seen[stats->label_count++] = n->label;
        }
    }
    stats->unique_labels = stats->label_count;
}

/*
 * Run a multi-phase pipeline with statistics gathering.
 */
pg_status pg_executor_run_pipeline(pg_executor *exec) {
    pg_plan_op *op = exec->plan->root;

    /* Phase 1: data access (scan / index scan) */
    while (op) {
        if (op->type == PG_OP_SCAN || op->type == PG_OP_INDEX_SCAN) {
            if (op->type == PG_OP_SCAN)
                pg_executor_run_scan(exec, op);
            else
                pg_executor_run_index_scan(exec, op);
            break;
        }
        op = op->child;
    }

    /* Phase 2: traversals */
    op = exec->plan->root;
    while (op) {
        if (op->type == PG_OP_TRAVERSE) {
            pg_executor_run_traverse(exec, op);
        }
        op = op->child;
    }

    /* Phase 3: join */
    op = exec->plan->root;
    while (op) {
        if (op->type == PG_OP_HASH_JOIN) {
            pg_executor_run_hash_join(exec, op);
        }
        op = op->child;
    }

    /* Phase 4: filter */
    pg_executor_run_filter(exec);

    /* Phase 5: project / aggregate / emit */
    op = exec->plan->root;
    while (op) {
        switch (op->type) {
        case PG_OP_PROJECT:
            pg_executor_run_project(exec);
            break;
        case PG_OP_AGGREGATE:
            pg_executor_run_aggregate(exec);
            break;
        case PG_OP_EMIT:
            exec->rows_produced = exec->result.node_count;
            break;
        default:
            break;
        }
        op = op->child;
    }

    return PG_OK;
}
