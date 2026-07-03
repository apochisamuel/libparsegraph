#include "matcher.h"
#include "parsegraph/graph.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_match_result_init(pg_match_result *mr) {
    mr->capacity = 16;
    mr->nodes = malloc(mr->capacity * sizeof(pg_node *));
    if (!mr->nodes) return PG_ERR_NOMEM;
    mr->edges = malloc(mr->capacity * sizeof(pg_edge *));
    if (!mr->edges) { free(mr->nodes); return PG_ERR_NOMEM; }
    mr->node_count = 0;
    mr->edge_count = 0;
    return PG_OK;
}

void pg_match_result_free(pg_match_result *mr) {
    free(mr->nodes);
    free(mr->edges);
    mr->nodes = NULL;
    mr->edges = NULL;
    mr->node_count = 0;
    mr->edge_count = 0;
}

pg_status pg_match_result_add_node(pg_match_result *mr, pg_node *n) {
    if (mr->node_count >= mr->capacity) {
        uint32_t nc = mr->capacity * 2;
        pg_node **nn = realloc(mr->nodes, nc * sizeof(pg_node *));
        if (!nn) return PG_ERR_NOMEM;
        mr->nodes = nn;
        pg_edge **ne = realloc(mr->edges, nc * sizeof(pg_edge *));
        if (!ne) return PG_ERR_NOMEM;
        mr->edges = ne;
        mr->capacity = nc;
    }
    mr->nodes[mr->node_count++] = n;
    return PG_OK;
}

pg_status pg_match_result_add_edge(pg_match_result *mr, pg_edge *e) {
    if (mr->edge_count >= mr->capacity) return PG_ERR_FULL;
    mr->edges[mr->edge_count++] = e;
    return PG_OK;
}

/*
 * BUG-1 CRASH SITE:
 * Reads cur->current_node->label.  If compaction has freed the
 * segment containing the node, this dereferences freed memory.
 *
 * BUG-3 CRASH SITE (secondary):
 * When restoring a backtrack checkpoint that references a released
 * snapshot, the cursor's node pointer (restored from snapshot) is
 * also stale, causing a UAF here.
 */
bool pg_match_node_predicate(pg_cursor *cur, pg_ast_node *pattern) {
    pg_node *node = pg_cursor_current(cur);
    if (!node) return false;

    if (pattern->type == PG_AST_NODE_PAT) {
        /* check label match */
        pg_label_id required = pattern->data.node_pat.label;
        if (required != PG_INVALID_LABEL) {
            return node->label == required;     /* ← crash: reads freed node */
        }
        return true;  /* wildcard match */
    }
    return false;
}

bool pg_match_edge_predicate(pg_graph *g, pg_edge_id eid,
                              pg_ast_node *edge_pat) {
    pg_edge *e = pg_graph_get_edge(g, eid);
    if (!e) return false;
    if (edge_pat->type != PG_AST_EDGE_PAT) return false;

    pg_label_id required = edge_pat->data.edge_pat.edge_type;
    if (required != PG_INVALID_LABEL) {
        return e->edge_type == required;
    }
    return true;
}

pg_status pg_match_pattern(pg_graph *g, pg_cursor *cur,
                            pg_ast_node *pattern, pg_match_result *result) {
    if (!pattern) return PG_OK;
    if (pattern->type != PG_AST_NODE_PAT) return PG_ERR_INVALID;

    /* try to match current cursor position */
    if (!pg_match_node_predicate(cur, pattern)) {
        return PG_ERR_NOT_FOUND;
    }

    pg_node *n = pg_cursor_current(cur);
    if (n) pg_match_result_add_node(result, n);

    /* if there's an edge pattern, follow edges */
    pg_ast_node *edge_pat = pattern->next;
    if (edge_pat && edge_pat->type == PG_AST_EDGE_PAT) {
        pg_cursor_follow_edge(cur, edge_pat->data.edge_pat.directed);

        pg_edge_id eid;
        while (pg_cursor_next_neighbor(cur, &eid)) {
            if (!pg_match_edge_predicate(g, eid, edge_pat)) continue;

            pg_edge *e = pg_graph_get_edge(g, eid);
            if (!e) continue;
            pg_match_result_add_edge(result, e);

            /* follow to destination node */
            if (edge_pat->next && edge_pat->next->type == PG_AST_NODE_PAT) {
                pg_node_id dst = edge_pat->data.edge_pat.directed
                                 ? e->dst : e->src;
                pg_cursor dst_cur;
                pg_cursor_init(&dst_cur, g);
                if (pg_cursor_seek_node(&dst_cur, dst) == PG_OK) {
                    pg_match_pattern(g, &dst_cur, edge_pat->next, result);
                }
                pg_cursor_close(&dst_cur);
            }
        }
    }
    return PG_OK;
}

/* ── Extended pattern matching ──────────────────────────── */

/*
 * Match a multi-hop path pattern: sequence of alternating
 * node and edge patterns.
 */
pg_status pg_match_path(pg_graph *g, pg_cursor *cur,
                         pg_ast_node *pattern, pg_match_result *result,
                         uint32_t max_depth) {
    if (!pattern || max_depth == 0) return PG_OK;

    /* try matching current position */
    if (!pg_match_node_predicate(cur, pattern))
        return PG_ERR_NOT_FOUND;

    pg_node *n = pg_cursor_current(cur);
    if (n) pg_match_result_add_node(result, n);

    /* follow edges if there's an edge pattern */
    pg_ast_node *edge_pat = pattern->next;
    if (!edge_pat || edge_pat->type != PG_AST_EDGE_PAT)
        return PG_OK;

    pg_ast_node *next_node_pat = edge_pat->next;

    pg_cursor_follow_edge(cur, edge_pat->data.edge_pat.directed);
    pg_edge_id eid;
    while (pg_cursor_next_neighbor(cur, &eid)) {
        if (!pg_match_edge_predicate(g, eid, edge_pat))
            continue;

        pg_edge *e = pg_graph_get_edge(g, eid);
        if (!e) continue;

        pg_match_result_add_edge(result, e);

        if (next_node_pat) {
            pg_node_id dst = edge_pat->data.edge_pat.directed
                             ? e->dst : e->src;
            pg_cursor dst_cur;
            pg_cursor_init(&dst_cur, g);
            if (pg_cursor_seek_node(&dst_cur, dst) == PG_OK) {
                pg_match_path(g, &dst_cur, next_node_pat, result,
                              max_depth - 1);
            }
            pg_cursor_close(&dst_cur);
        }
    }

    return PG_OK;
}

/*
 * Count matches for a pattern without materializing results.
 * Used by the cost model to estimate selectivity.
 */
uint32_t pg_match_count(pg_graph *g, pg_ast_node *pattern) {
    if (!pattern || pattern->type != PG_AST_NODE_PAT)
        return 0;

    uint32_t count = 0;
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_node *n = (pg_node *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (n->deleted_gen != 0) continue;

            pg_label_id required = pattern->data.node_pat.label;
            if (required == PG_INVALID_LABEL || n->label == required) {
                count++;
            }
        }
        seg = seg->next;
    }
    return count;
}

/*
 * Filter match results by property value.
 * Returns a new result set containing only nodes where the
 * specified property matches the given value.
 */
pg_status pg_match_filter_by_prop(pg_match_result *input,
                                   pg_prop_key key,
                                   const pg_value *expected,
                                   pg_match_result *output) {
    pg_match_result_init(output);

    for (uint32_t i = 0; i < input->node_count; i++) {
        pg_node *n = input->nodes[i];
        if (!n) continue;

        pg_value *actual = pg_props_get(&n->properties, key);
        if (!actual) continue;

        bool match = false;
        if (actual->type == expected->type) {
            switch (actual->type) {
            case PG_VAL_INT:
                match = actual->v.i == expected->v.i;
                break;
            case PG_VAL_BOOL:
                match = actual->v.b == expected->v.b;
                break;
            case PG_VAL_FLOAT:
                match = actual->v.f == expected->v.f;
                break;
            case PG_VAL_STRING:
                if (actual->v.str.len == expected->v.str.len) {
                    match = memcmp(actual->v.str.data, expected->v.str.data,
                                   actual->v.str.len) == 0;
                }
                break;
            default:
                break;
            }
        }

        if (match) {
            pg_match_result_add_node(output, n);
        }
    }

    return PG_OK;
}

/*
 * Merge two match results, removing duplicates by node ID.
 */
pg_status pg_match_result_merge(pg_match_result *dst,
                                 const pg_match_result *src) {
    for (uint32_t i = 0; i < src->node_count; i++) {
        pg_node *n = src->nodes[i];
        if (!n) continue;

        /* check for duplicates */
        bool exists = false;
        for (uint32_t j = 0; j < dst->node_count; j++) {
            if (dst->nodes[j] && dst->nodes[j]->id == n->id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            pg_match_result_add_node(dst, n);
        }
    }
    return PG_OK;
}
