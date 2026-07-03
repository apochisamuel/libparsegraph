#include "snapshot.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_snapshot_create(pg_snapshot **out, pg_generation gen) {
    pg_snapshot *s = calloc(1, sizeof(pg_snapshot));
    if (!s) return PG_ERR_NOMEM;
    s->gen = gen;
    s->ref_capacity = 32;
    s->refs = malloc(s->ref_capacity * sizeof(pg_snap_ref));
    if (!s->refs) { free(s); return PG_ERR_NOMEM; }
    s->ref_count = 0;
    s->refcount = 1;
    s->released = false;
    *out = s;
    return PG_OK;
}

void pg_snapshot_retain(pg_snapshot *snap) {
    if (snap) snap->refcount++;
}

/*
 * BUG-3 FREE SITE:
 * When the refcount drops to zero, we free the snapshot and its
 * refs array.  If a backtrack checkpoint has saved a SnapshotRef
 * pointing into this snapshot's node pointers, the checkpoint's
 * restore will dereference freed memory.
 *
 * The refcount only tracks explicit retain/release pairs from
 * the executor.  Backtrack checkpoints create a second independent
 * claim on the snapshot that the refcount doesn't know about.
 * The missing abstraction is a BacktrackLease that prevents release
 * while checkpoints are outstanding.
 */
void pg_snapshot_release(pg_snapshot *snap) {
    if (!snap) return;
    if (snap->refcount > 0) snap->refcount--;
    if (snap->refcount == 0) {
        free(snap->refs);                 /* ← free site */
        snap->refs = NULL;
        snap->ref_count = 0;
        snap->released = true;
        free(snap);
    }
}

pg_status pg_snapshot_add_ref(pg_snapshot *snap, pg_node *node,
                               pg_node_id id) {
    if (snap->ref_count >= snap->ref_capacity) {
        uint32_t nc = snap->ref_capacity * 2;
        pg_snap_ref *nr = realloc(snap->refs, nc * sizeof(pg_snap_ref));
        if (!nr) return PG_ERR_NOMEM;
        snap->refs = nr;
        snap->ref_capacity = nc;
    }
    snap->refs[snap->ref_count].node = node;
    snap->refs[snap->ref_count].id = id;
    snap->ref_count++;
    return PG_OK;
}

pg_node *pg_snapshot_resolve(pg_snapshot *snap, pg_node_id id) {
    for (uint32_t i = 0; i < snap->ref_count; i++) {
        if (snap->refs[i].id == id)
            return snap->refs[i].node;
    }
    return NULL;
}

pg_generation pg_snapshot_gen(const pg_snapshot *snap) {
    return snap->gen;
}

uint32_t pg_snapshot_ref_count(const pg_snapshot *snap) {
    return snap->ref_count;
}

/* ── Extended snapshot operations ────────────────────────── */

/*
 * Check if a node is visible in the snapshot (created before snapshot gen).
 */
bool pg_snapshot_is_visible(pg_snapshot *snap, pg_node *node) {
    if (!snap || !node) return false;
    return node->created_gen <= snap->gen &&
           (node->deleted_gen == 0 || node->deleted_gen > snap->gen);
}

/*
 * Iterate all node references in the snapshot.
 */
typedef bool (*pg_snap_iter_fn)(pg_node *node, pg_node_id id, void *ctx);

void pg_snapshot_foreach(pg_snapshot *snap, pg_snap_iter_fn fn, void *ctx) {
    if (!snap) return;
    for (uint32_t i = 0; i < snap->ref_count; i++) {
        if (!fn(snap->refs[i].node, snap->refs[i].id, ctx)) return;
    }
}

/*
 * Count the number of valid references in the snapshot.
 */
uint32_t pg_snapshot_valid_count(pg_snapshot *snap) {
    if (!snap) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < snap->ref_count; i++) {
        if (snap->refs[i].node) count++;
    }
    return count;
}

/*
 * Check if snapshot has been released.
 */
bool pg_snapshot_is_released(const pg_snapshot *snap) {
    return !snap || snap->released;
}

/*
 * Create a diff between two snapshots: find nodes that exist
 * in snap_new but not in snap_old.
 */
uint32_t pg_snapshot_diff(pg_snapshot *snap_old, pg_snapshot *snap_new,
                           pg_node_id *added, uint32_t max_added) {
    uint32_t count = 0;
    if (!snap_old || !snap_new) return 0;

    for (uint32_t i = 0; i < snap_new->ref_count && count < max_added; i++) {
        pg_node_id id = snap_new->refs[i].id;
        /* check if this id exists in old snapshot */
        bool found = false;
        for (uint32_t j = 0; j < snap_old->ref_count; j++) {
            if (snap_old->refs[j].id == id) { found = true; break; }
        }
        if (!found) {
            added[count++] = id;
        }
    }
    return count;
}
