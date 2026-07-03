#ifndef PG_OPTIMIZER_H
#define PG_OPTIMIZER_H

#include "parsegraph/types.h"
#include "ast.h"
#include "planner.h"

typedef struct pg_optimizer {
    pg_query_plan   *plan;
    pg_ast_node     *captured_filter;  /* filter from pushdown pass */
    uint32_t         pass_count;
} pg_optimizer;

pg_status pg_optimizer_init(pg_optimizer *opt, pg_query_plan *plan);
pg_status pg_optimizer_run(pg_optimizer *opt);
pg_status pg_optimizer_pushdown(pg_optimizer *opt);
pg_status pg_optimizer_join_reorder(pg_optimizer *opt);

#endif
