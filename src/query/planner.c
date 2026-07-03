#include "planner.h"
#include "cost.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static pg_plan_op *plan_pattern(pg_ast_node *pat) {
    if (!pat) return NULL;

    pg_plan_op *op = NULL;
    if (pat->type == PG_AST_NODE_PAT) {
        /* decide: label scan vs full scan */
        if (pat->data.node_pat.label != PG_INVALID_LABEL) {
            op = pg_plan_op_create(PG_OP_INDEX_SCAN);
            if (op) op->label = pat->data.node_pat.label;
        } else {
            op = pg_plan_op_create(PG_OP_SCAN);
        }
    }

    /* check for edge traversal in chain */
    pg_ast_node *edge = pat->next;
    if (edge && edge->type == PG_AST_EDGE_PAT && op) {
        pg_plan_op *trav = pg_plan_op_create(PG_OP_TRAVERSE);
        if (trav) {
            trav->edge_type = edge->data.edge_pat.edge_type;
            trav->child = op;
            op = trav;
        }
        /* recurse for destination */
        if (edge->next) {
            pg_plan_op *dst_op = plan_pattern(edge->next);
            if (dst_op) {
                pg_plan_op *join = pg_plan_op_create(PG_OP_HASH_JOIN);
                if (join) {
                    join->child = op;
                    pg_plan_op *t = op;
                    while (t->next) t = t->next;
                    t->next = dst_op;
                    op = join;
                }
            }
        }
    }
    return op;
}

pg_status pg_planner_create_plan(pg_ast_node *ast, pg_query_plan **out) {
    pg_query_plan *plan = calloc(1, sizeof(pg_query_plan));
    if (!plan) return PG_ERR_NOMEM;

    plan->ast = ast;
    plan->total_cost = 0.0f;
    plan->est_rows = 100;
    plan->optimized = false;

    /* translate AST to physical plan */
    pg_plan_op *root_op = NULL;
    pg_ast_node *node = ast;
    pg_plan_op *last_op = NULL;

    while (node) {
        pg_plan_op *op = NULL;
        switch (node->type) {
        case PG_AST_MATCH:
            op = plan_pattern(node->data.match.pattern);
            if (node->data.match.where_clause) {
                pg_plan_op *filt = pg_plan_op_create(PG_OP_FILTER);
                if (filt) {
                    filt->child = op;
                    op = filt;
                }
            }
            break;
        case PG_AST_PROJECT: {
            op = pg_plan_op_create(PG_OP_PROJECT);
            if (op) {
                op->column_count = node->data.project.column_count;
                if (node->data.project.columns) {
                    size_t sz = op->column_count * sizeof(uint32_t);
                    op->columns = malloc(sz);
                    if (op->columns) memcpy(op->columns,
                        node->data.project.columns, sz);
                }
            }
            break;
        }
        case PG_AST_AGGREGATE:
            op = pg_plan_op_create(PG_OP_AGGREGATE);
            if (op) {
                op->agg_func = (uint8_t)node->data.aggregate.func;
                op->agg_column = (uint8_t)node->data.aggregate.column;
            }
            break;
        default:
            break;
        }
        if (op) {
            if (!root_op) root_op = op;
            else if (last_op) {
                /* chain: previous op feeds into this one */
                pg_plan_op *t = last_op;
                while (t->child) t = t->child;
                t->child = op;
            }
            last_op = op;
        }
        node = node->next;
    }

    /* add emit at the end */
    pg_plan_op *emit = pg_plan_op_create(PG_OP_EMIT);
    if (emit) {
        if (last_op) {
            pg_plan_op *t = last_op;
            while (t->child) t = t->child;
            t->child = emit;
        } else {
            root_op = emit;
        }
    }

    plan->root = root_op;
    *out = plan;
    return PG_OK;
}

void pg_planner_free_plan(pg_query_plan *plan) {
    if (!plan) return;
    pg_plan_op_free(plan->root);
    /* AST is owned by caller or optimizer; don't free here */
    free(plan);
}

/* ── Extended planner ────────────────────────────────────── */

/*
 * Estimate the cost of the entire plan.
 */
float pg_planner_estimate_cost(pg_query_plan *plan) {
    if (!plan || !plan->root) return 0.0f;
    return pg_cost_estimate_plan(plan->root);
}

/*
 * Count the number of operations in the plan tree.
 */
static uint32_t count_ops(pg_plan_op *op) {
    if (!op) return 0;
    return 1 + count_ops(op->child) + count_ops(op->next);
}

uint32_t pg_planner_op_count(pg_query_plan *plan) {
    return plan ? count_ops(plan->root) : 0;
}

/*
 * Print the plan tree (for debugging).
 */
static const char *op_type_name(pg_plan_op_type type) {
    switch (type) {
    case PG_OP_SCAN:       return "SCAN";
    case PG_OP_INDEX_SCAN: return "INDEX_SCAN";
    case PG_OP_FILTER:     return "FILTER";
    case PG_OP_PROJECT:    return "PROJECT";
    case PG_OP_AGGREGATE:  return "AGGREGATE";
    case PG_OP_HASH_JOIN:  return "HASH_JOIN";
    case PG_OP_TRAVERSE:   return "TRAVERSE";
    case PG_OP_EMIT:       return "EMIT";
    default:               return "UNKNOWN";
    }
}

static void print_plan_indent(pg_plan_op *op, int depth) {
    if (!op) return;
    for (int i = 0; i < depth; i++) printf("  ");
    printf("%s (est_rows=%u, cost=%.1f)\n",
           op_type_name(op->type), op->est_rows, op->est_cost);
    print_plan_indent(op->child, depth + 1);
    print_plan_indent(op->next, depth);
}

void pg_planner_print(pg_query_plan *plan) {
    if (!plan) return;
    printf("=== Query Plan (optimized=%s) ===\n",
           plan->optimized ? "yes" : "no");
    print_plan_indent(plan->root, 0);
    printf("Total cost: %.1f, est_rows: %u\n",
           plan->total_cost, plan->est_rows);
}

/*
 * Deep-clone a plan (creates independent copy).
 */
static pg_plan_op *clone_op(pg_plan_op *op) {
    if (!op) return NULL;
    pg_plan_op *c = pg_plan_op_create(op->type);
    if (!c) return NULL;
    c->label = op->label;
    c->edge_type = op->edge_type;
    c->est_rows = op->est_rows;
    c->est_cost = op->est_cost;
    c->agg_func = op->agg_func;
    c->agg_column = op->agg_column;
    if (op->columns && op->column_count > 0) {
        c->columns = malloc(op->column_count * sizeof(uint32_t));
        if (c->columns) {
            memcpy(c->columns, op->columns,
                   op->column_count * sizeof(uint32_t));
            c->column_count = op->column_count;
        }
    }
    c->child = clone_op(op->child);
    c->next = clone_op(op->next);
    return c;
}

pg_query_plan *pg_planner_clone_plan(pg_query_plan *plan) {
    if (!plan) return NULL;
    pg_query_plan *c = calloc(1, sizeof(pg_query_plan));
    if (!c) return NULL;
    c->root = clone_op(plan->root);
    c->ast = plan->ast;  /* shared; not cloned */
    c->total_cost = plan->total_cost;
    c->est_rows = plan->est_rows;
    c->optimized = plan->optimized;
    return c;
}
