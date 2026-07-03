#ifndef PG_PROJECT_H
#define PG_PROJECT_H

#include "parsegraph/types.h"
#include "matcher.h"

typedef struct pg_project_ctx {
    uint32_t *columns;
    uint32_t  column_count;
    pg_value *output_row;
    uint32_t  output_count;
} pg_project_ctx;

pg_status pg_project_init(pg_project_ctx *pc, uint32_t *columns,
                           uint32_t count);
void      pg_project_free(pg_project_ctx *pc);
pg_status pg_project_result_field(pg_project_ctx *pc,
                                   pg_match_result *result,
                                   uint32_t col_idx, pg_value *out);
pg_status pg_project_row(pg_project_ctx *pc, pg_match_result *result);

#endif
