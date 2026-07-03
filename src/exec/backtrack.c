#include "backtrack.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_backtrack_init(pg_backtrack_stack *bs) {
    bs->capacity = 8;
    bs->points = calloc(bs->capacity, sizeof(pg_backtrack_point));
    if (!bs->points) return PG_ERR_NOMEM;
    bs->count = 0;
    return PG_OK;
}

void pg_backtrack_free(pg_backtrack_stack *bs) {
    for (uint32_t i = 0; i < bs->count; i++) {
        if (bs->points[i].active) {
            pg_match_result_free(&bs->points[i].saved_result);
        }
    }
    free(bs->points);
    bs->points = NULL;
    bs->count = 0;
}

/*
 * BUG-3 ALIAS SITE:
 * Saves a checkpoint containing a snapshot reference.  The snapshot
 * holds raw pointers to graph nodes.  We do NOT retain the snapshot
 * here — we just store the pointer, creating an independent claim
 * that the snapshot's refcount doesn't track.
 *
 * When the executor releases the snapshot (refcount→0), the snapshot
 * and its node references are freed.  But this checkpoint still
 * references it via bp->snapshot.
 */
pg_status pg_backtrack_save(pg_backtrack_stack *bs, pg_snapshot *snap,
                             pg_cursor *cur, uint32_t depth,
                             pg_match_result *result) {
    if (bs->count >= bs->capacity) {
        uint32_t nc = bs->capacity * 2;
        pg_backtrack_point *np = realloc(bs->points,
                                          nc * sizeof(pg_backtrack_point));
        if (!np) return PG_ERR_NOMEM;
        bs->points = np;
        bs->capacity = nc;
    }

    pg_backtrack_point *bp = &bs->points[bs->count];
    bp->snapshot = snap;                  /* ← alias: raw pointer, no retain */
    bp->cursor_node = cur->current_id;
    bp->match_depth = depth;
    bp->active = true;

    /* save match result (shallow copy of node pointers) */
    pg_match_result_init(&bp->saved_result);
    for (uint32_t i = 0; i < result->node_count; i++) {
        pg_match_result_add_node(&bp->saved_result, result->nodes[i]);
    }
    for (uint32_t i = 0; i < result->edge_count; i++) {
        pg_match_result_add_edge(&bp->saved_result, result->edges[i]);
    }

    bs->count++;
    return PG_OK;
}

/*
 * BUG-3 CRASH SITE:
 * Restores a checkpoint whose snapshot may have been freed.
 * The snapshot's node pointers are then used to re-populate
 * the match result and seek the cursor — all reading freed memory.
 */
pg_status pg_backtrack_restore(pg_backtrack_stack *bs, pg_cursor *cur,
                                uint32_t *depth, pg_match_result *result) {
    if (bs->count == 0) return PG_ERR_NOT_FOUND;

    pg_backtrack_point *bp = &bs->points[bs->count - 1];
    if (!bp->active) return PG_ERR_NOT_FOUND;

    /* restore cursor from snapshot */
    if (bp->snapshot) {
        /* resolve node from snapshot — if snapshot is freed, UAF here */
        pg_node *n = pg_snapshot_resolve(bp->snapshot,     /* ← crash site */
                                          bp->cursor_node);
        if (n) {
            cur->current_node = n;
            cur->current_id = bp->cursor_node;
        }
    }

    *depth = bp->match_depth;

    /* restore result */
    pg_match_result_free(result);
    *result = bp->saved_result;
    memset(&bp->saved_result, 0, sizeof(pg_match_result));

    bp->active = false;
    bs->count--;
    return PG_OK;
}

bool pg_backtrack_has_points(const pg_backtrack_stack *bs) {
    return bs->count > 0;
}

/* ── Extended backtrack operations ───────────────────────── */

/*
 * Count the number of active checkpoints.
 */
uint32_t pg_backtrack_count(const pg_backtrack_stack *bs) {
    return bs->count;
}

/*
 * Peek at the top checkpoint without removing it.
 */
const pg_backtrack_point *pg_backtrack_peek(const pg_backtrack_stack *bs) {
    if (bs->count == 0) return NULL;
    return &bs->points[bs->count - 1];
}

/*
 * Clear all checkpoints.
 */
void pg_backtrack_clear(pg_backtrack_stack *bs) {
    for (uint32_t i = 0; i < bs->count; i++) {
        if (bs->points[i].active) {
            pg_match_result_free(&bs->points[i].saved_result);
            bs->points[i].active = false;
        }
    }
    bs->count = 0;
}

/*
 * Get the total depth across all checkpoints.
 */
uint32_t pg_backtrack_total_depth(const pg_backtrack_stack *bs) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < bs->count; i++) {
        if (bs->points[i].active) {
            total += bs->points[i].match_depth;
        }
    }
    return total;
}

/*
 * Save a checkpoint with a retained snapshot.
 * Unlike backtrack_save, this increments the snapshot's refcount
 * to prevent premature release.
 */
pg_status pg_backtrack_save_retained(pg_backtrack_stack *bs,
                                       pg_snapshot *snap,
                                       pg_cursor *cur,
                                       uint32_t depth,
                                       pg_match_result *result) {
    pg_snapshot_retain(snap);
    return pg_backtrack_save(bs, snap, cur, depth, result);
}
