#include "plan_ops.h"
#include <stdlib.h>
#include <string.h>

pg_plan_op *pg_plan_op_create(pg_plan_op_type type) {
    pg_plan_op *op = calloc(1, sizeof(pg_plan_op));
    if (!op) return NULL;
    op->type = type;
    op->est_rows = 100;
    op->est_cost = 1.0f;
    return op;
}

void pg_plan_op_free(pg_plan_op *op) {
    if (!op) return;
    pg_plan_op_free(op->child);
    pg_plan_op_free(op->next);
    free(op->columns);
    free(op);
}
