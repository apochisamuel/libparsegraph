#include "batch.h"
#include "parsegraph/graph.h"
#include "../query/parser.h"
#include "../query/planner.h"
#include "../query/optimizer.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_batch_init(pg_batch_ctx *bc, pg_session *sess) {
    bc->session = sess;
    bc->op_capacity = PG_MAX_BATCH_OPS;
    bc->ops = calloc(bc->op_capacity, sizeof(pg_batch_op));
    if (!bc->ops) return PG_ERR_NOMEM;
    bc->op_count = 0;
    bc->executed = 0;
    return PG_OK;
}

void pg_batch_free(pg_batch_ctx *bc) {
    for (uint32_t i = 0; i < bc->op_count; i++) {
        if (bc->ops[i].type == PG_BATCH_SET_PROP) {
            pg_value_free(&bc->ops[i].data.set_prop.val);
        }
    }
    free(bc->ops);
    bc->ops = NULL;
    bc->op_count = 0;
}

pg_status pg_batch_add_op(pg_batch_ctx *bc, const pg_batch_op *op) {
    if (bc->op_count >= bc->op_capacity) return PG_ERR_FULL;
    bc->ops[bc->op_count] = *op;
    bc->op_count++;
    return PG_OK;
}

/*
 * Batch execution: processes a sequence of interleaved mutations
 * and queries.  This is the trigger site for bugs 2, 4, 5, 6
 * because mutations modify graph state between query steps,
 * invalidating cached pointers.
 */
pg_status pg_batch_execute(pg_batch_ctx *bc) {
    pg_graph *g = bc->session->graph;

    for (uint32_t i = 0; i < bc->op_count; i++) {
        pg_batch_op *op = &bc->ops[i];

        switch (op->type) {
        case PG_BATCH_ADD_NODE: {
            pg_graph_add_node(g, op->data.add_node.label);
            break;
        }
        case PG_BATCH_ADD_EDGE: {
            /* BUG-4 TRIGGER: add_edge → adjacency_append → realloc */
            pg_graph_add_edge(g, op->data.add_edge.src,
                              op->data.add_edge.dst,
                              op->data.add_edge.type);
            break;
        }
        case PG_BATCH_DEL_NODE: {
            /* BUG-2 TRIGGER: delete node frees arena slot while
               memo cache holds stale pointers */
            pg_graph_remove_node(g, op->data.del_node.id);
            break;
        }
        case PG_BATCH_SET_PROP: {
            /* BUG-5 TRIGGER: set_prop frees old PropertyValue while
               filter stage holds a cached pointer */
            pg_graph_set_node_prop(g, op->data.set_prop.id,
                                   op->data.set_prop.key,
                                   &op->data.set_prop.val);
            break;
        }
        case PG_BATCH_QUERY: {
            pg_parse_ctx pctx = {
                .input = op->data.query.data,
                .len = op->data.query.len,
                .pos = 0,
                .intern_label = NULL,
                .intern_ctx = NULL
            };
            pg_ast_node *ast = NULL;
            if (pg_parse_query(&pctx, &ast) == PG_OK && ast) {
                pg_query_plan *plan = NULL;
                if (pg_planner_create_plan(ast, &plan) == PG_OK) {
                    pg_optimizer opt;
                    pg_optimizer_init(&opt, plan);
                    pg_optimizer_run(&opt);

                    pg_session_execute_query(bc->session, plan);
                    pg_planner_free_plan(plan);
                }
                pg_ast_free(ast);
            }
            break;
        }
        case PG_BATCH_COMPACT:
            /* trigger compaction — covered by protocol handler */
            break;
        }
        bc->executed++;
    }
    return PG_OK;
}

/* ── Extended batch operations ───────────────────────────── */

/*
 * Execute a batch with progress tracking.
 * Returns the number of operations successfully executed.
 */
uint32_t pg_batch_execute_tracked(pg_batch_ctx *bc) {
    pg_batch_execute(bc);
    return bc->executed;
}

/*
 * Validate batch operations before execution.
 * Checks that referenced node/edge IDs exist.
 */
pg_status pg_batch_validate(pg_batch_ctx *bc) {
    pg_graph *g = bc->session->graph;

    for (uint32_t i = 0; i < bc->op_count; i++) {
        pg_batch_op *op = &bc->ops[i];

        switch (op->type) {
        case PG_BATCH_ADD_EDGE:
            if (!pg_graph_get_node(g, op->data.add_edge.src) ||
                !pg_graph_get_node(g, op->data.add_edge.dst)) {
                return PG_ERR_NOT_FOUND;
            }
            break;
        case PG_BATCH_DEL_NODE:
            if (!pg_graph_get_node(g, op->data.del_node.id)) {
                return PG_ERR_NOT_FOUND;
            }
            break;
        case PG_BATCH_SET_PROP:
            if (!pg_graph_get_node(g, op->data.set_prop.id)) {
                return PG_ERR_NOT_FOUND;
            }
            break;
        default:
            break;
        }
    }
    return PG_OK;
}

/*
 * Create a batch of random mutations for testing.
 * Adds a sequence of add_node → add_edge → set_prop operations.
 */
pg_status pg_batch_create_test_mutations(pg_batch_ctx *bc,
                                          uint32_t count,
                                          pg_label_id label) {
    for (uint32_t i = 0; i < count && bc->op_count < bc->op_capacity; i++) {
        pg_batch_op op;
        memset(&op, 0, sizeof(op));

        if (i % 3 == 0) {
            op.type = PG_BATCH_ADD_NODE;
            op.data.add_node.label = label;
        } else if (i % 3 == 1) {
            op.type = PG_BATCH_SET_PROP;
            op.data.set_prop.id = (pg_node_id)(i / 3 + 1);
            op.data.set_prop.key = (pg_prop_key)(i % 8);
            op.data.set_prop.val.type = PG_VAL_INT;
            op.data.set_prop.val.v.i = (int64_t)i * 100;
        } else {
            op.type = PG_BATCH_ADD_EDGE;
            op.data.add_edge.src = (pg_node_id)(i / 3);
            op.data.add_edge.dst = (pg_node_id)(i / 3 + 1);
            op.data.add_edge.type = label;
        }

        pg_batch_add_op(bc, &op);
    }
    return PG_OK;
}
