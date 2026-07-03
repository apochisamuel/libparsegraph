#ifndef PG_AGGREGATE_H
#define PG_AGGREGATE_H

#include "parsegraph/types.h"
#include "../query/ast.h"

typedef struct pg_agg_ctx {
    pg_agg_func func;
    uint32_t    column;
    int64_t     int_accum;
    double      float_accum;
    uint32_t    count;
    bool        has_value;
} pg_agg_ctx;

pg_status pg_agg_init(pg_agg_ctx *ac, pg_agg_func func, uint32_t column);
void      pg_agg_reset(pg_agg_ctx *ac);
pg_status pg_agg_accumulate(pg_agg_ctx *ac, pg_value *val);
pg_status pg_agg_result(pg_agg_ctx *ac, pg_value *out);

#endif
