#ifndef PG_EXECUTOR_H
#define PG_EXECUTOR_H

#include "parsegraph/types.h"
#include "cursor.h"
#include "matcher.h"
#include "filter.h"
#include "project.h"
#include "aggregate.h"
#include "join.h"
#include "backtrack.h"
#include "../query/planner.h"
#include "../core/snapshot.h"

struct pg_executor {
    pg_graph            *graph;
    pg_query_plan       *plan;
    pg_cursor            cursor;
    pg_match_result      result;
    pg_filter_ctx        filter;
    pg_project_ctx       project;
    pg_agg_ctx           aggregate;
    pg_join_ctx          join;
    pg_backtrack_stack   backtrack;
    pg_snapshot         *snapshot;
    bool                 has_filter;
    bool                 has_project;
    bool                 has_aggregate;
    bool                 has_join;
    uint32_t             rows_produced;
};

pg_status pg_executor_create(pg_executor **out, pg_graph *g,
                              pg_query_plan *plan);
void      pg_executor_destroy(pg_executor *exec);
pg_status pg_executor_run(pg_executor *exec);
pg_status pg_executor_run_scan(pg_executor *exec, pg_plan_op *op);
pg_status pg_executor_run_index_scan(pg_executor *exec, pg_plan_op *op);
pg_status pg_executor_run_filter(pg_executor *exec);
pg_status pg_executor_run_project(pg_executor *exec);
pg_status pg_executor_run_aggregate(pg_executor *exec);
pg_status pg_executor_run_pattern(pg_executor *exec, pg_ast_node *pattern);

#endif
