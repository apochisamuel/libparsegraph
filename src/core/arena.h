#ifndef PG_ARENA_H
#define PG_ARENA_H

#include "parsegraph/types.h"

/*
 * Arena allocator for graph nodes and edges.
 * Manages segments of contiguous slots; supports compaction
 * by relocating live objects to a fresh segment and freeing
 * the old one.
 */

typedef struct pg_arena_segment {
    struct pg_arena_segment *next;
    uint32_t  slot_size;
    uint32_t  slot_count;
    uint32_t  used;
    uint32_t  live;        /* slots not yet freed */
    uint32_t  segment_id;
    uint8_t  *data;        /* separately allocated slot array */
    uint8_t  *occupied;    /* bitmap: 1 per slot */
} pg_arena_segment;

typedef struct pg_arena {
    pg_arena_segment *segments;
    uint32_t  slot_size;
    uint32_t  segment_capacity;
    uint32_t  next_segment_id;
    uint32_t  total_live;
    uint32_t  total_slots;
} pg_arena;

pg_status          pg_arena_init(pg_arena *a, uint32_t slot_size,
                                 uint32_t segment_cap);
void               pg_arena_destroy(pg_arena *a);
void              *pg_arena_alloc(pg_arena *a);
void               pg_arena_free_slot(pg_arena *a, void *ptr);
float              pg_arena_utilization(const pg_arena *a);
pg_arena_segment  *pg_arena_find_segment(pg_arena *a, void *ptr);
pg_arena_segment  *pg_arena_active_segment(pg_arena *a);

/* compaction: allocate a fresh segment, caller relocates live objects,
   then call release_segment on the old one */
pg_arena_segment  *pg_arena_new_segment(pg_arena *a);
void               pg_arena_release_segment(pg_arena *a,
                                             pg_arena_segment *seg);

#endif /* PG_ARENA_H */
