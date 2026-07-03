#ifndef PG_SEGMENT_H
#define PG_SEGMENT_H

#include "parsegraph/types.h"
#include "../core/arena.h"

/*
 * Segment manager wraps arena segments with compaction metadata.
 * Tracks fragmentation and decides when compaction is worthwhile.
 */

typedef struct pg_segment_info {
    pg_arena_segment *arena_seg;
    uint32_t          total_allocs;
    uint32_t          total_frees;
    float             frag_ratio;
    bool              compacting;
} pg_segment_info;

typedef struct pg_segment_mgr {
    pg_arena         *arena;
    pg_segment_info  *infos;
    uint32_t          info_count;
    uint32_t          info_capacity;
    uint32_t          compact_threshold_ops;
} pg_segment_mgr;

pg_status pg_segment_mgr_init(pg_segment_mgr *sm, pg_arena *arena);
void      pg_segment_mgr_free(pg_segment_mgr *sm);
void      pg_segment_mgr_track_alloc(pg_segment_mgr *sm,
                                      pg_arena_segment *seg);
void      pg_segment_mgr_track_free(pg_segment_mgr *sm,
                                     pg_arena_segment *seg);
bool      pg_segment_mgr_should_compact(pg_segment_mgr *sm);
pg_segment_info *pg_segment_mgr_worst(pg_segment_mgr *sm);

#endif /* PG_SEGMENT_H */
