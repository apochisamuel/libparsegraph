#include "filter.h"
#include "matcher.h"
#include "parsegraph/graph.h"
#include "../core/properties.h"

pg_status pg_filter_init(pg_filter_ctx *fc, pg_ast_node *predicate) {
    fc->predicate = predicate;
    fc->cached_prop_value = NULL;
    fc->source_node = NULL;
    fc->passed = false;
    return PG_OK;
}

void pg_filter_reset(pg_filter_ctx *fc) {
    fc->cached_prop_value = NULL;
    fc->source_node = NULL;
    fc->passed = false;
}

/*
 * BUG-5 ALIAS SITE:
 * Caches a raw pointer to the PropertyValue from the node's
 * property storage.  If the property is overwritten (pg_props_set)
 * during a batch mutation, the old value is freed and this pointer
 * becomes dangling.
 *
 * The missing abstraction is a property-read pin that prevents
 * the value from being freed while pipeline stages hold references.
 */
pg_value *pg_filter_read_property(pg_filter_ctx *fc, pg_node *node,
                                   pg_prop_key key) {
    pg_value *v = pg_props_get(&node->properties, key);
    fc->cached_prop_value = v;          /* ← alias: caches raw pointer */
    fc->source_node = node;
    return v;
}

static bool eval_expr(pg_filter_ctx *fc, pg_ast_node *expr, pg_node *node) {
    if (!expr) return true;

    switch (expr->type) {
    case PG_AST_LITERAL:
        return expr->data.literal.value.type == PG_VAL_BOOL
               ? expr->data.literal.value.v.b : true;
    case PG_AST_BINOP: {
        switch (expr->data.binop.op) {
        case PG_BINOP_AND:
            return eval_expr(fc, expr->data.binop.left, node) &&
                   eval_expr(fc, expr->data.binop.right, node);
        case PG_BINOP_OR:
            return eval_expr(fc, expr->data.binop.left, node) ||
                   eval_expr(fc, expr->data.binop.right, node);
        case PG_BINOP_EQ: {
            /* simple: ident == literal comparison */
            pg_ast_node *left = expr->data.binop.left;
            pg_ast_node *right = expr->data.binop.right;
            if (left && left->type == PG_AST_IDENT &&
                right && right->type == PG_AST_LITERAL) {
                /* property lookup — this caches the value */
                pg_prop_key key = left->data.ident.len;
                pg_value *pv = pg_filter_read_property(fc, node, key);
                if (!pv) return false;
                if (pv->type == PG_VAL_INT && right->data.literal.value.type == PG_VAL_INT) {
                    return pv->v.i == right->data.literal.value.v.i;
                }
            }
            return false;
        }
        case PG_BINOP_LT: {
            pg_ast_node *left = expr->data.binop.left;
            pg_ast_node *right = expr->data.binop.right;
            if (left && left->type == PG_AST_IDENT &&
                right && right->type == PG_AST_LITERAL) {
                pg_prop_key key = left->data.ident.len;
                pg_value *pv = pg_filter_read_property(fc, node, key);
                if (!pv) return false;
                if (pv->type == PG_VAL_INT && right->data.literal.value.type == PG_VAL_INT)
                    return pv->v.i < right->data.literal.value.v.i;
            }
            return false;
        }
        default: return true;
        }
    }
    default:
        return true;
    }
}

bool pg_filter_evaluate(pg_filter_ctx *fc, pg_node *node, pg_graph *graph) {
    (void)graph;
    if (!fc->predicate) return true;
    fc->passed = eval_expr(fc, fc->predicate, node);
    return fc->passed;
}

/* ── Extended filter operations ──────────────────────────── */

/*
 * Multi-predicate filter: evaluate a chain of AND/OR predicates.
 */
bool pg_filter_evaluate_chain(pg_filter_ctx *fc, pg_node *node,
                                pg_graph *graph, pg_ast_node *chain) {
    if (!chain) return true;

    pg_ast_node *pred = chain;
    while (pred) {
        pg_filter_reset(fc);
        bool passed = pg_filter_evaluate(fc, node, graph);
        if (!passed) return false;
        pred = pred->next;
    }
    return true;
}

/*
 * Filter by label.
 */
bool pg_filter_by_label(pg_node *node, pg_label_id label) {
    if (!node) return false;
    if (label == PG_INVALID_LABEL) return true;
    return node->label == label;
}

/*
 * Filter by property existence.
 */
bool pg_filter_has_property(pg_node *node, pg_prop_key key) {
    if (!node) return false;
    return pg_props_get(&node->properties, key) != NULL;
}

/*
 * Filter by property range (integer only).
 */
bool pg_filter_prop_range(pg_node *node, pg_prop_key key,
                           int64_t min_val, int64_t max_val) {
    if (!node) return false;
    pg_value *v = pg_props_get(&node->properties, key);
    if (!v || v->type != PG_VAL_INT) return false;
    return v->v.i >= min_val && v->v.i <= max_val;
}

/*
 * Count nodes in a result that pass the filter.
 */
uint32_t pg_filter_count_passing(pg_filter_ctx *fc,
                                   pg_match_result *result,
                                   pg_graph *graph) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < result->node_count; i++) {
        pg_node *n = result->nodes[i];
        if (!n) continue;
        pg_filter_reset(fc);
        if (pg_filter_evaluate(fc, n, graph)) count++;
    }
    return count;
}
