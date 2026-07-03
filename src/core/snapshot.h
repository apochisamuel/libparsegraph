#ifndef PG_SNAPSHOT_H
#define PG_SNAPSHOT_H

#include "parsegraph/types.h"

/*
 * A snapshot captures the graph generation at a point in time.
 * Queries read through snapshots to get a consistent view.
 * Snapshots hold references to nodes that were live at capture time.
 */

typedef struct pg_snap_ref {
    pg_node    *node;
    pg_node_id  id;
} pg_snap_ref;

struct pg_snapshot {
    pg_generation  gen;
    pg_snap_ref   *refs;
    uint32_t       ref_count;
    uint32_t       ref_capacity;
    uint32_t       refcount;     /* reference count for sharing */
    bool           released;
};

pg_status    pg_snapshot_create(pg_snapshot **out, pg_generation gen);
void         pg_snapshot_retain(pg_snapshot *snap);
void         pg_snapshot_release(pg_snapshot *snap);
pg_status    pg_snapshot_add_ref(pg_snapshot *snap, pg_node *node,
                                 pg_node_id id);
pg_node     *pg_snapshot_resolve(pg_snapshot *snap, pg_node_id id);
pg_generation pg_snapshot_gen(const pg_snapshot *snap);
uint32_t     pg_snapshot_ref_count(const pg_snapshot *snap);

#endif /* PG_SNAPSHOT_H */
