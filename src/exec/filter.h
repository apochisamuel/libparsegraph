#ifndef PG_FILTER_H
#define PG_FILTER_H

#include "parsegraph/types.h"
#include "../query/ast.h"

typedef struct pg_filter_ctx {
    pg_ast_node *predicate;
    pg_value    *cached_prop_value;   /* BUG-5 ALIAS: cached PropertyValue* */
    pg_node     *source_node;
    bool         passed;
} pg_filter_ctx;

pg_status pg_filter_init(pg_filter_ctx *fc, pg_ast_node *predicate);
void      pg_filter_reset(pg_filter_ctx *fc);
bool      pg_filter_evaluate(pg_filter_ctx *fc, pg_node *node,
                              pg_graph *graph);
pg_value *pg_filter_read_property(pg_filter_ctx *fc, pg_node *node,
                                   pg_prop_key key);

#endif
