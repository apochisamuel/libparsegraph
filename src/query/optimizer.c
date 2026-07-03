#include "optimizer.h"
#include "cost.h"
#include <stdlib.h>

pg_status pg_optimizer_init(pg_optimizer *opt, pg_query_plan *plan) {
    opt->plan = plan;
    opt->captured_filter = NULL;
    opt->pass_count = 0;
    return PG_OK;
}

/*
 * BUG-9 ALIAS SITE:
 * Predicate pushdown captures a raw pointer to the AST FilterNode
 * so cost estimation can evaluate its selectivity later.
 * This pointer is stored in opt->captured_filter.
 *
 * The problem: join_reorder (pass 2) restructures the AST and may
 * call pg_ast_replace_subtree() on the region containing this filter,
 * freeing the old subtree.  opt->captured_filter is now dangling.
 */
pg_status pg_optimizer_pushdown(pg_optimizer *opt) {
    pg_ast_node *ast = opt->plan->ast;
    if (!ast) return PG_OK;

    /* find filter in the AST */
    if (ast->type == PG_AST_MATCH && ast->data.match.where_clause) {
        pg_ast_node *filter = ast->data.match.where_clause;
        if (filter->type == PG_AST_FILTER) {
            /* capture pointer for cost estimation */
            opt->captured_filter = filter;          /* ← alias site */

            /* estimate selectivity based on predicate structure */
            if (filter->data.filter.predicate) {
                pg_ast_node *pred = filter->data.filter.predicate;
                if (pred->type == PG_AST_BINOP &&
                    pred->data.binop.op == PG_BINOP_EQ) {
                    filter->data.filter.selectivity = 0.1f;
                }
            }
        }
    }

    opt->pass_count++;
    return PG_OK;
}

pg_status pg_optimizer_join_reorder(pg_optimizer *opt) {
    pg_ast_node *ast = opt->plan->ast;
    if (!ast) return PG_OK;

    /* look for join patterns to reorder */
    if (ast->type == PG_AST_MATCH && ast->data.match.pattern) {
        pg_ast_node *pat = ast->data.match.pattern;

        /* if there's a chain with edge traversal, consider flipping
           the join order for better selectivity */
        if (pat->next && pat->next->type == PG_AST_EDGE_PAT) {
            pg_ast_node *edge = pat->next;
            if (edge->next && edge->next->type == PG_AST_NODE_PAT) {
                pg_ast_node *dst = edge->next;

                /* restructure: if dst label has better selectivity,
                   swap source and destination */
                if (dst->data.node_pat.label > pat->data.node_pat.label) {
                    /* create a new restructured pattern */
                    pg_ast_node *new_pat = pg_ast_clone(dst);
                    if (new_pat) {
                        pg_ast_node *new_edge = pg_ast_create(PG_AST_EDGE_PAT);
                        if (new_edge) {
                            new_edge->data.edge_pat = edge->data.edge_pat;
                            new_edge->data.edge_pat.directed = !edge->data.edge_pat.directed;
                            pg_ast_node *new_src = pg_ast_clone(pat);
                            new_edge->next = new_src;
                            new_pat->next = new_edge;
                        }

                        /* replace the pattern subtree — this frees the old
                           pattern chain including any filter nodes that were
                           part of the where clause if they were attached.
                           BUG-9: if pushdown attached the filter to the
                           pattern, this frees it while captured_filter
                           still points to it. */
                        pg_ast_replace_subtree(&ast->data.match.pattern,
                                               new_pat);

                        /* also recreate the where clause if filter was moved */
                        if (ast->data.match.where_clause &&
                            opt->captured_filter == ast->data.match.where_clause) {
                            pg_ast_node *new_filt = pg_ast_create(PG_AST_FILTER);
                            if (new_filt) {
                                new_filt->data.filter.selectivity = 0.5f;
                                new_filt->data.filter.column_refs = 1;
                                pg_ast_replace_subtree(
                                    &ast->data.match.where_clause, new_filt);
                                /* opt->captured_filter is now stale */
                            }
                        }
                    }
                }
            }
        }
    }

    opt->pass_count++;
    return PG_OK;
}

pg_status pg_optimizer_run(pg_optimizer *opt) {
    pg_status s;
    s = pg_optimizer_pushdown(opt);
    if (s != PG_OK) return s;
    s = pg_optimizer_join_reorder(opt);
    if (s != PG_OK) return s;

    /* cost estimation uses captured_filter which may be stale */
    if (opt->captured_filter) {
        float cost = pg_cost_estimate_filter(opt->captured_filter);
        opt->plan->total_cost += cost;
    }

    opt->plan->optimized = true;
    return PG_OK;
}

/* ── Additional optimization passes ─────────────────────── */

/*
 * Dead-column elimination: remove columns from the projection
 * that are not referenced by any downstream operation.
 */
static void opt_dead_column_elim(pg_optimizer *opt) {
    pg_ast_node *ast = opt->plan->ast;
    if (!ast) return;

    /* walk the AST looking for PROJECT nodes */
    pg_ast_node *node = ast;
    while (node) {
        if (node->type == PG_AST_PROJECT && node->data.project.columns) {
            /* check if any aggregate references columns not in projection */
            pg_ast_node *next = node->next;
            if (next && next->type == PG_AST_AGGREGATE) {
                uint32_t agg_col = next->data.aggregate.column;
                bool found = false;
                for (uint32_t i = 0; i < node->data.project.column_count; i++) {
                    if (node->data.project.columns[i] == agg_col) {
                        found = true;
                        break;
                    }
                }
                /* if aggregate column not in projection, add it */
                if (!found && node->data.project.column_count < 16) {
                    uint32_t new_count = node->data.project.column_count + 1;
                    uint32_t *new_cols = realloc(
                        node->data.project.columns,
                        new_count * sizeof(uint32_t));
                    if (new_cols) {
                        new_cols[new_count - 1] = agg_col;
                        node->data.project.columns = new_cols;
                        node->data.project.column_count = new_count;
                    }
                }
            }
        }
        node = node->next;
    }
    opt->pass_count++;
}

/*
 * Constant folding: evaluate constant expressions in filter
 * predicates at plan time rather than at execution time.
 */
static void opt_constant_fold(pg_optimizer *opt) {
    pg_ast_node *ast = opt->plan->ast;
    if (!ast || ast->type != PG_AST_MATCH) return;

    pg_ast_node *where = ast->data.match.where_clause;
    if (!where || where->type != PG_AST_FILTER) return;
    if (!where->data.filter.predicate) return;

    pg_ast_node *pred = where->data.filter.predicate;
    if (pred->type == PG_AST_BINOP) {
        pg_ast_node *left = pred->data.binop.left;
        pg_ast_node *right = pred->data.binop.right;

        /* if both sides are literals, evaluate now */
        if (left && left->type == PG_AST_LITERAL &&
            right && right->type == PG_AST_LITERAL) {
            bool result = false;
            if (left->data.literal.value.type == PG_VAL_INT &&
                right->data.literal.value.type == PG_VAL_INT) {
                switch (pred->data.binop.op) {
                case PG_BINOP_EQ:
                    result = left->data.literal.value.v.i ==
                             right->data.literal.value.v.i;
                    break;
                case PG_BINOP_LT:
                    result = left->data.literal.value.v.i <
                             right->data.literal.value.v.i;
                    break;
                case PG_BINOP_GT:
                    result = left->data.literal.value.v.i >
                             right->data.literal.value.v.i;
                    break;
                default:
                    break;
                }
            }

            /* replace predicate with boolean literal */
            pg_ast_node *folded = pg_ast_create(PG_AST_LITERAL);
            if (folded) {
                folded->data.literal.value.type = PG_VAL_BOOL;
                folded->data.literal.value.v.b = result;
                pg_ast_replace_subtree(&where->data.filter.predicate,
                                        folded);

                /* if always-true, selectivity is 1.0 */
                where->data.filter.selectivity = result ? 1.0f : 0.0f;
            }
        }
    }
    opt->pass_count++;
}

/*
 * Scan type selection: choose between full scan and index scan
 * based on the plan cost model estimates.
 */
static void opt_scan_selection(pg_optimizer *opt) {
    pg_plan_op *op = opt->plan->root;
    while (op) {
        if (op->type == PG_OP_SCAN && op->label != PG_INVALID_LABEL) {
            /* if we have a label, prefer index scan */
            op->type = PG_OP_INDEX_SCAN;
            op->est_cost *= 0.3f;
        }
        op = op->child;
    }
    opt->pass_count++;
}
