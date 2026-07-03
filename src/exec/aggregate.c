#include "aggregate.h"
#include "matcher.h"
#include "../core/properties.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

pg_status pg_agg_init(pg_agg_ctx *ac, pg_agg_func func, uint32_t column) {
    ac->func = func;
    ac->column = column;
    ac->int_accum = 0;
    ac->float_accum = 0.0;
    ac->count = 0;
    ac->has_value = false;
    return PG_OK;
}

void pg_agg_reset(pg_agg_ctx *ac) {
    ac->int_accum = 0;
    ac->float_accum = 0.0;
    ac->count = 0;
    ac->has_value = false;
}

/*
 * BUG-5 CRASH SITE:
 * Reads val->v.i (or val->v.f).  If the pg_value was freed by
 * pg_props_set during a batch mutation (Bug 5 free site), and
 * val is the cached_prop_value from the filter stage, this
 * dereferences freed memory → heap-use-after-free.
 */
pg_status pg_agg_accumulate(pg_agg_ctx *ac, pg_value *val) {
    if (!val) return PG_OK;

    switch (ac->func) {
    case PG_AGG_COUNT:
        ac->count++;
        ac->has_value = true;
        break;
    case PG_AGG_SUM:
        if (val->type == PG_VAL_INT) {
            ac->int_accum += val->v.i;          /* ← crash site */
            ac->has_value = true;
        } else if (val->type == PG_VAL_FLOAT) {
            ac->float_accum += val->v.f;
            ac->has_value = true;
        }
        ac->count++;
        break;
    case PG_AGG_AVG:
        if (val->type == PG_VAL_INT) {
            ac->float_accum += (double)val->v.i;
            ac->has_value = true;
        } else if (val->type == PG_VAL_FLOAT) {
            ac->float_accum += val->v.f;
            ac->has_value = true;
        }
        ac->count++;
        break;
    case PG_AGG_MIN:
        if (val->type == PG_VAL_INT) {
            if (!ac->has_value || val->v.i < ac->int_accum)
                ac->int_accum = val->v.i;
            ac->has_value = true;
        }
        ac->count++;
        break;
    case PG_AGG_MAX:
        if (val->type == PG_VAL_INT) {
            if (!ac->has_value || val->v.i > ac->int_accum)
                ac->int_accum = val->v.i;
            ac->has_value = true;
        }
        ac->count++;
        break;
    }
    return PG_OK;
}

pg_status pg_agg_result(pg_agg_ctx *ac, pg_value *out) {
    out->type = PG_VAL_INT;
    switch (ac->func) {
    case PG_AGG_COUNT:
        out->v.i = (int64_t)ac->count;
        break;
    case PG_AGG_SUM:
        out->v.i = ac->int_accum;
        break;
    case PG_AGG_AVG:
        if (ac->count > 0) {
            out->type = PG_VAL_FLOAT;
            out->v.f = ac->float_accum / (double)ac->count;
        } else {
            out->v.i = 0;
        }
        break;
    case PG_AGG_MIN:
    case PG_AGG_MAX:
        out->v.i = ac->int_accum;
        break;
    }
    return PG_OK;
}

/* ── Extended aggregation ────────────────────────────────── */

/*
 * Accumulate from a match result directly.
 */
pg_status pg_agg_accumulate_result(pg_agg_ctx *ac,
                                    pg_match_result *result,
                                    pg_prop_key key) {
    for (uint32_t i = 0; i < result->node_count; i++) {
        pg_node *n = result->nodes[i];
        if (!n) continue;
        pg_value *v = pg_props_get(&n->properties, key);
        if (v) pg_agg_accumulate(ac, v);
    }
    return PG_OK;
}

/*
 * Combine two aggregation contexts (for parallel aggregation).
 */
pg_status pg_agg_merge(pg_agg_ctx *dst, const pg_agg_ctx *src) {
    switch (dst->func) {
    case PG_AGG_COUNT:
        dst->count += src->count;
        dst->has_value = dst->has_value || src->has_value;
        break;
    case PG_AGG_SUM:
        dst->int_accum += src->int_accum;
        dst->float_accum += src->float_accum;
        dst->count += src->count;
        dst->has_value = dst->has_value || src->has_value;
        break;
    case PG_AGG_AVG:
        dst->float_accum += src->float_accum;
        dst->count += src->count;
        dst->has_value = dst->has_value || src->has_value;
        break;
    case PG_AGG_MIN:
        if (src->has_value && (!dst->has_value ||
            src->int_accum < dst->int_accum)) {
            dst->int_accum = src->int_accum;
            dst->has_value = true;
        }
        dst->count += src->count;
        break;
    case PG_AGG_MAX:
        if (src->has_value && (!dst->has_value ||
            src->int_accum > dst->int_accum)) {
            dst->int_accum = src->int_accum;
            dst->has_value = true;
        }
        dst->count += src->count;
        break;
    }
    return PG_OK;
}

/*
 * Format the aggregation result as a string.
 */
int pg_agg_format(pg_agg_ctx *ac, char *buf, size_t buflen) {
    pg_value result;
    pg_agg_result(ac, &result);
    
    switch (result.type) {
    case PG_VAL_INT:
        return snprintf(buf, buflen, "%lld", (long long)result.v.i);
    case PG_VAL_FLOAT:
        return snprintf(buf, buflen, "%.4f", result.v.f);
    default:
        return snprintf(buf, buflen, "(none)");
    }
}
