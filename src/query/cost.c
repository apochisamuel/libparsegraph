#include "cost.h"
#include <math.h>

/*
 * BUG-9 CRASH SITE:
 * cost_estimate_filter reads filter->data.filter.selectivity and
 * filter->data.filter.column_refs.  If the optimizer's captured_filter
 * pointer is stale (freed during join_reorder), this dereferences
 * freed heap memory → heap-use-after-free.
 */
float pg_cost_estimate_filter(pg_ast_node *filter) {
    if (!filter) return 0.0f;
    if (filter->type != PG_AST_FILTER) return 1.0f;

    float sel = filter->data.filter.selectivity;    /* ← crash site */
    uint32_t refs = filter->data.filter.column_refs;

    /* base cost: row-scan factor * selectivity * column access cost */
    float base = 100.0f * sel;
    float col_cost = (float)refs * 0.5f;
    return base + col_cost;
}

float pg_cost_estimate_scan(pg_plan_op *op, uint32_t table_rows) {
    if (!op) return 0.0f;
    float base = (float)table_rows;
    if (op->type == PG_OP_INDEX_SCAN) {
        base *= 0.3f;  /* index reduces scan cost */
    }
    return base;
}

float pg_cost_estimate_join(pg_plan_op *op, uint32_t left_rows,
                             uint32_t right_rows) {
    if (!op) return 0.0f;
    /* hash join cost model: build + probe */
    float build_cost = (float)left_rows * 1.2f;
    float probe_cost = (float)right_rows * 0.8f;
    return build_cost + probe_cost;
}

float pg_cost_estimate_plan(pg_plan_op *root) {
    if (!root) return 0.0f;
    float cost = 0.0f;
    pg_plan_op *op = root;
    while (op) {
        switch (op->type) {
        case PG_OP_SCAN:
            cost += pg_cost_estimate_scan(op, op->est_rows);
            break;
        case PG_OP_INDEX_SCAN:
            cost += pg_cost_estimate_scan(op, op->est_rows);
            break;
        case PG_OP_FILTER:
            cost += op->est_rows * 0.5f;
            break;
        case PG_OP_HASH_JOIN:
            cost += pg_cost_estimate_join(op, op->est_rows, op->est_rows);
            break;
        case PG_OP_TRAVERSE:
            cost += op->est_rows * 2.0f;
            break;
        case PG_OP_PROJECT:
            cost += op->est_rows * 0.1f;
            break;
        case PG_OP_AGGREGATE:
            cost += op->est_rows * 0.3f;
            break;
        case PG_OP_EMIT:
            cost += op->est_rows * 0.05f;
            break;
        }
        op = op->child;
    }
    return cost;
}

/* ── Extended cost model ──────────────────────────────────── */

/*
 * Estimate the cardinality (number of output rows) for a plan op.
 */
uint32_t pg_cost_estimate_cardinality(pg_plan_op *op) {
    if (!op) return 0;

    switch (op->type) {
    case PG_OP_SCAN:
        return op->est_rows;
    case PG_OP_INDEX_SCAN:
        return (uint32_t)((float)op->est_rows * 0.3f);
    case PG_OP_FILTER:
        return (uint32_t)((float)op->est_rows * 0.5f);
    case PG_OP_HASH_JOIN: {
        uint32_t left = op->child ? pg_cost_estimate_cardinality(op->child) : op->est_rows;
        return (uint32_t)((float)left * 0.1f);
    }
    case PG_OP_TRAVERSE:
        return op->est_rows * 3;
    case PG_OP_PROJECT:
        return op->child ? pg_cost_estimate_cardinality(op->child) : op->est_rows;
    case PG_OP_AGGREGATE:
        return 1;  /* aggregation typically produces one row */
    default:
        return op->est_rows;
    }
}

/*
 * Estimate memory usage for a plan operation.
 */
uint32_t pg_cost_estimate_memory(pg_plan_op *op) {
    if (!op) return 0;

    switch (op->type) {
    case PG_OP_HASH_JOIN:
        /* hash table size ≈ build rows * entry size */
        return op->est_rows * 64;
    case PG_OP_SCAN:
    case PG_OP_INDEX_SCAN:
        /* scan buffer ≈ batch size * row size */
        return 4096;
    case PG_OP_AGGREGATE:
        /* accumulator state */
        return 256;
    default:
        return 0;
    }
}

/*
 * Compare two plans and return which is cheaper.
 */
int pg_cost_compare_plans(pg_plan_op *a, pg_plan_op *b) {
    float cost_a = pg_cost_estimate_plan(a);
    float cost_b = pg_cost_estimate_plan(b);
    if (cost_a < cost_b) return -1;
    if (cost_a > cost_b) return 1;
    return 0;
}

/*
 * Compute the selectivity of a filter based on the operator type.
 */
float pg_cost_selectivity(pg_binop op) {
    switch (op) {
    case PG_BINOP_EQ:  return 0.1f;
    case PG_BINOP_NEQ: return 0.9f;
    case PG_BINOP_LT:  return 0.33f;
    case PG_BINOP_GT:  return 0.33f;
    case PG_BINOP_AND: return 0.25f;
    case PG_BINOP_OR:  return 0.75f;
    default:           return 0.5f;
    }
}

/*
 * Estimate I/O cost for a plan (pages read/written).
 */
float pg_cost_estimate_io(pg_plan_op *root) {
    if (!root) return 0.0f;
    float io = 0.0f;
    pg_plan_op *op = root;
    while (op) {
        switch (op->type) {
        case PG_OP_SCAN:
            io += (float)op->est_rows * 0.01f;
            break;
        case PG_OP_INDEX_SCAN:
            io += (float)op->est_rows * 0.003f;
            break;
        case PG_OP_HASH_JOIN:
            io += (float)op->est_rows * 0.02f;
            break;
        default:
            break;
        }
        op = op->child;
    }
    return io;
}
