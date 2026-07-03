#ifndef PG_COST_H
#define PG_COST_H

#include "parsegraph/types.h"
#include "ast.h"
#include "plan_ops.h"

float pg_cost_estimate_filter(pg_ast_node *filter);
float pg_cost_estimate_scan(pg_plan_op *op, uint32_t table_rows);
float pg_cost_estimate_join(pg_plan_op *op, uint32_t left_rows,
                             uint32_t right_rows);
float pg_cost_estimate_plan(pg_plan_op *root);

#endif
