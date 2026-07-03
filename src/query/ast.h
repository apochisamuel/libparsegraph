#ifndef PG_AST_H
#define PG_AST_H

#include "parsegraph/types.h"

typedef enum {
    PG_AST_MATCH,
    PG_AST_FILTER,
    PG_AST_PROJECT,
    PG_AST_AGGREGATE,
    PG_AST_JOIN,
    PG_AST_RETURN,
    PG_AST_NODE_PAT,
    PG_AST_EDGE_PAT,
    PG_AST_LITERAL,
    PG_AST_IDENT,
    PG_AST_BINOP,
    PG_AST_FUNC_CALL
} pg_ast_type;

typedef enum {
    PG_BINOP_EQ, PG_BINOP_NEQ,
    PG_BINOP_LT, PG_BINOP_GT,
    PG_BINOP_AND, PG_BINOP_OR,
    PG_BINOP_ADD, PG_BINOP_SUB
} pg_binop;

typedef enum {
    PG_AGG_COUNT, PG_AGG_SUM, PG_AGG_AVG, PG_AGG_MIN, PG_AGG_MAX
} pg_agg_func;

typedef struct pg_ast_node {
    pg_ast_type  type;
    union {
        struct { struct pg_ast_node *pattern; struct pg_ast_node *where_clause; } match;
        struct { struct pg_ast_node *predicate; float selectivity; uint32_t column_refs; } filter;
        struct { uint32_t *columns; uint32_t column_count; } project;
        struct { pg_agg_func func; uint32_t column; } aggregate;
        struct { struct pg_ast_node *left; struct pg_ast_node *right; uint32_t join_key; } join;
        struct { pg_label_id label; char *alias; uint32_t alias_len; } node_pat;
        struct { pg_label_id edge_type; bool directed; } edge_pat;
        struct { pg_value value; } literal;
        struct { char *name; uint32_t len; } ident;
        struct { pg_binop op; struct pg_ast_node *left; struct pg_ast_node *right; } binop;
    } data;
    struct pg_ast_node *next;
    uint32_t refcount;
} pg_ast_node;

pg_ast_node *pg_ast_create(pg_ast_type type);
void         pg_ast_free(pg_ast_node *node);
pg_ast_node *pg_ast_clone(const pg_ast_node *node);
void         pg_ast_replace_subtree(pg_ast_node **slot, pg_ast_node *replacement);

#endif
