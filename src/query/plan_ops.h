#ifndef PG_PLAN_OPS_H
#define PG_PLAN_OPS_H

#include "parsegraph/types.h"

typedef enum {
    PG_OP_SCAN,
    PG_OP_INDEX_SCAN,
    PG_OP_FILTER,
    PG_OP_PROJECT,
    PG_OP_AGGREGATE,
    PG_OP_HASH_JOIN,
    PG_OP_TRAVERSE,
    PG_OP_EMIT
} pg_plan_op_type;

typedef struct pg_plan_op {
    pg_plan_op_type  type;
    pg_label_id      label;
    pg_label_id      edge_type;
    uint32_t         est_rows;
    float            est_cost;
    uint32_t        *columns;
    uint32_t         column_count;
    uint8_t          agg_func;
    uint8_t          agg_column;
    struct pg_plan_op *child;
    struct pg_plan_op *next;
} pg_plan_op;

pg_plan_op *pg_plan_op_create(pg_plan_op_type type);
void        pg_plan_op_free(pg_plan_op *op);

#endif
