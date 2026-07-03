#include "ast.h"
#include "../core/properties.h"
#include <stdlib.h>
#include <string.h>

pg_ast_node *pg_ast_create(pg_ast_type type) {
    pg_ast_node *n = calloc(1, sizeof(pg_ast_node));
    if (!n) return NULL;
    n->type = type;
    n->refcount = 1;
    return n;
}

void pg_ast_free(pg_ast_node *node) {
    if (!node) return;
    pg_ast_node *sib = node->next;
    while (sib) {
        pg_ast_node *nx = sib->next;
        sib->next = NULL;
        pg_ast_free(sib);
        sib = nx;
    }

    switch (node->type) {
    case PG_AST_MATCH:
        pg_ast_free(node->data.match.pattern);
        pg_ast_free(node->data.match.where_clause);
        break;
    case PG_AST_FILTER:
        pg_ast_free(node->data.filter.predicate);
        break;
    case PG_AST_PROJECT:
        free(node->data.project.columns);
        break;
    case PG_AST_JOIN:
        pg_ast_free(node->data.join.left);
        pg_ast_free(node->data.join.right);
        break;
    case PG_AST_NODE_PAT:
        free(node->data.node_pat.alias);
        break;
    case PG_AST_LITERAL:
        pg_value_free(&node->data.literal.value);
        break;
    case PG_AST_IDENT:
        free(node->data.ident.name);
        break;
    case PG_AST_BINOP:
        pg_ast_free(node->data.binop.left);
        pg_ast_free(node->data.binop.right);
        break;
    default: break;
    }
    free(node);
}

pg_ast_node *pg_ast_clone(const pg_ast_node *node) {
    if (!node) return NULL;
    pg_ast_node *c = pg_ast_create(node->type);
    if (!c) return NULL;

    switch (node->type) {
    case PG_AST_MATCH:
        c->data.match.pattern = pg_ast_clone(node->data.match.pattern);
        c->data.match.where_clause = pg_ast_clone(node->data.match.where_clause);
        break;
    case PG_AST_FILTER:
        c->data.filter = node->data.filter;
        c->data.filter.predicate = pg_ast_clone(node->data.filter.predicate);
        break;
    case PG_AST_PROJECT:
        c->data.project.column_count = node->data.project.column_count;
        if (node->data.project.columns) {
            size_t sz = node->data.project.column_count * sizeof(uint32_t);
            c->data.project.columns = malloc(sz);
            if (c->data.project.columns)
                memcpy(c->data.project.columns, node->data.project.columns, sz);
        }
        break;
    case PG_AST_JOIN:
        c->data.join.join_key = node->data.join.join_key;
        c->data.join.left = pg_ast_clone(node->data.join.left);
        c->data.join.right = pg_ast_clone(node->data.join.right);
        break;
    case PG_AST_NODE_PAT:
        c->data.node_pat.label = node->data.node_pat.label;
        if (node->data.node_pat.alias) {
            c->data.node_pat.alias = malloc(node->data.node_pat.alias_len + 1);
            if (c->data.node_pat.alias) {
                memcpy(c->data.node_pat.alias, node->data.node_pat.alias,
                       node->data.node_pat.alias_len + 1);
                c->data.node_pat.alias_len = node->data.node_pat.alias_len;
            }
        }
        break;
    case PG_AST_EDGE_PAT:
        c->data.edge_pat = node->data.edge_pat;
        break;
    case PG_AST_LITERAL:
        pg_value_copy(&c->data.literal.value, &node->data.literal.value);
        break;
    case PG_AST_IDENT:
        if (node->data.ident.name) {
            c->data.ident.name = malloc(node->data.ident.len + 1);
            if (c->data.ident.name) {
                memcpy(c->data.ident.name, node->data.ident.name,
                       node->data.ident.len + 1);
                c->data.ident.len = node->data.ident.len;
            }
        }
        break;
    case PG_AST_BINOP:
        c->data.binop.op = node->data.binop.op;
        c->data.binop.left = pg_ast_clone(node->data.binop.left);
        c->data.binop.right = pg_ast_clone(node->data.binop.right);
        break;
    default: break;
    }
    if (node->next) c->next = pg_ast_clone(node->next);
    return c;
}

/*
 * BUG-9 FREE SITE:
 * Replaces a subtree in-place, freeing the old one immediately.
 * If optimizer pass 1 (predicate pushdown) captured a FilterNode*
 * and pass 2 (join reorder) calls replace_subtree on the same region,
 * the old FilterNode is freed.  cost_estimate_filter() later reads
 * the stale pointer → heap-use-after-free.
 */
void pg_ast_replace_subtree(pg_ast_node **slot, pg_ast_node *replacement) {
    pg_ast_node *old = *slot;
    *slot = replacement;
    if (old) pg_ast_free(old);
}
