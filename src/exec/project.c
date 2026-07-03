#include "project.h"
#include "../core/properties.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_project_init(pg_project_ctx *pc, uint32_t *columns,
                           uint32_t count) {
    pc->column_count = count;
    pc->columns = malloc(count * sizeof(uint32_t));
    if (!pc->columns) return PG_ERR_NOMEM;
    memcpy(pc->columns, columns, count * sizeof(uint32_t));
    pc->output_row = calloc(count, sizeof(pg_value));
    if (!pc->output_row) { free(pc->columns); return PG_ERR_NOMEM; }
    pc->output_count = 0;
    return PG_OK;
}

void pg_project_free(pg_project_ctx *pc) {
    free(pc->columns);
    for (uint32_t i = 0; i < pc->output_count; i++)
        pg_value_free(&pc->output_row[i]);
    free(pc->output_row);
}

/*
 * BUG-2 CRASH SITE:
 * Reads node->properties through a pointer stored in the match result.
 * If the node has been freed via graph_remove_node (which frees the
 * arena slot), the node pointer in the result is dangling.
 *
 * This happens when:
 *  1. Query runs, produces MatchResult with node pointers → cached in memo
 *  2. Batch mutation deletes a matched node (arena slot freed)
 *  3. Later query step retrieves the cached result and projects
 */
pg_status pg_project_result_field(pg_project_ctx *pc,
                                   pg_match_result *result,
                                   uint32_t col_idx, pg_value *out) {
    if (col_idx >= result->node_count) return PG_ERR_NOT_FOUND;

    pg_node *node = result->nodes[col_idx];      /* potentially stale */
    if (!node) return PG_ERR_NOT_FOUND;

    /* read node id — this is the actual crash point for Bug 2 */
    out->type = PG_VAL_INT;
    out->v.i = (int64_t)node->id;                /* ← crash site */
    return PG_OK;
}

pg_status pg_project_row(pg_project_ctx *pc, pg_match_result *result) {
    for (uint32_t i = 0; i < pc->column_count; i++) {
        uint32_t col = pc->columns[i];
        pg_status s = pg_project_result_field(pc, result, col,
                                               &pc->output_row[i]);
        if (s != PG_OK) return s;
    }
    pc->output_count = pc->column_count;
    return PG_OK;
}

/* ── Extended projection ─────────────────────────────────── */

/*
 * Project multiple rows from a match result.
 * Produces an array of output values.
 */
pg_status pg_project_all_rows(pg_project_ctx *pc,
                                pg_match_result *result,
                                pg_value *out, uint32_t max_rows,
                                uint32_t *rows_written) {
    *rows_written = 0;
    uint32_t out_idx = 0;

    for (uint32_t r = 0; r < result->node_count && *rows_written < max_rows; r++) {
        for (uint32_t c = 0; c < pc->column_count; c++) {
            pg_value val;
            memset(&val, 0, sizeof(val));
            pg_project_result_field(pc, result, r, &val);
            if (out_idx < max_rows * pc->column_count) {
                out[out_idx++] = val;
            }
        }
        (*rows_written)++;
    }
    return PG_OK;
}

/*
 * Project only the specified column from each matched node.
 * Returns node property values rather than node IDs.
 */
pg_status pg_project_column(pg_match_result *result,
                             pg_prop_key prop_key,
                             pg_value *out, uint32_t max,
                             uint32_t *count) {
    *count = 0;
    for (uint32_t i = 0; i < result->node_count && *count < max; i++) {
        pg_node *n = result->nodes[i];
        if (!n) continue;
        pg_value *v = pg_props_get(&n->properties, prop_key);
        if (v) {
            pg_value_copy(&out[*count], v);
            (*count)++;
        }
    }
    return PG_OK;
}

/*
 * Distinct projection: only include unique values.
 */
pg_status pg_project_distinct(pg_match_result *result,
                               pg_prop_key prop_key,
                               pg_value *out, uint32_t max,
                               uint32_t *count) {
    *count = 0;
    for (uint32_t i = 0; i < result->node_count && *count < max; i++) {
        pg_node *n = result->nodes[i];
        if (!n) continue;
        pg_value *v = pg_props_get(&n->properties, prop_key);
        if (!v) continue;

        /* check for duplicates */
        bool dup = false;
        for (uint32_t j = 0; j < *count; j++) {
            if (pg_value_equals(&out[j], v)) { dup = true; break; }
        }
        if (!dup) {
            pg_value_copy(&out[*count], v);
            (*count)++;
        }
    }
    return PG_OK;
}
