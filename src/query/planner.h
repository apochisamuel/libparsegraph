#ifndef PG_PLANNER_H
#define PG_PLANNER_H

#include "parsegraph/types.h"
#include "ast.h"
#include "plan_ops.h"

struct pg_query_plan {
    pg_plan_op  *root;
    pg_ast_node *ast;         /* owned reference to the AST */
    float        total_cost;
    uint32_t     est_rows;
    bool         optimized;
};

pg_status pg_planner_create_plan(pg_ast_node *ast, pg_query_plan **out);
void      pg_planner_free_plan(pg_query_plan *plan);

#endif
