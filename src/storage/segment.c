#include "segment.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_segment_mgr_init(pg_segment_mgr *sm, pg_arena *arena) {
    sm->arena = arena;
    sm->info_capacity = 16;
    sm->infos = calloc(sm->info_capacity, sizeof(pg_segment_info));
    if (!sm->infos) return PG_ERR_NOMEM;
    sm->info_count = 0;
    sm->compact_threshold_ops = 64;
    return PG_OK;
}

void pg_segment_mgr_free(pg_segment_mgr *sm) {
    free(sm->infos);
    sm->infos = NULL;
    sm->info_count = 0;
}

static pg_segment_info *find_or_create(pg_segment_mgr *sm,
                                        pg_arena_segment *seg) {
    for (uint32_t i = 0; i < sm->info_count; i++) {
        if (sm->infos[i].arena_seg == seg) return &sm->infos[i];
    }
    if (sm->info_count >= sm->info_capacity) {
        uint32_t nc = sm->info_capacity * 2;
        pg_segment_info *ni = realloc(sm->infos, nc * sizeof(pg_segment_info));
        if (!ni) return NULL;
        sm->infos = ni;
        sm->info_capacity = nc;
    }
    pg_segment_info *info = &sm->infos[sm->info_count++];
    memset(info, 0, sizeof(*info));
    info->arena_seg = seg;
    return info;
}

void pg_segment_mgr_track_alloc(pg_segment_mgr *sm,
                                 pg_arena_segment *seg) {
    pg_segment_info *info = find_or_create(sm, seg);
    if (info) {
        info->total_allocs++;
        if (info->total_allocs > 0) {
            info->frag_ratio = (float)info->total_frees /
                               (float)info->total_allocs;
        }
    }
}

void pg_segment_mgr_track_free(pg_segment_mgr *sm,
                                pg_arena_segment *seg) {
    pg_segment_info *info = find_or_create(sm, seg);
    if (info) {
        info->total_frees++;
        if (info->total_allocs > 0) {
            info->frag_ratio = (float)info->total_frees /
                               (float)info->total_allocs;
        }
    }
}

bool pg_segment_mgr_should_compact(pg_segment_mgr *sm) {
    for (uint32_t i = 0; i < sm->info_count; i++) {
        if (sm->infos[i].frag_ratio > PG_COMPACT_THRESHOLD &&
            sm->infos[i].total_allocs >= sm->compact_threshold_ops) {
            return true;
        }
    }
    return false;
}

pg_segment_info *pg_segment_mgr_worst(pg_segment_mgr *sm) {
    pg_segment_info *worst = NULL;
    float worst_frag = 0.0f;
    for (uint32_t i = 0; i < sm->info_count; i++) {
        if (sm->infos[i].frag_ratio > worst_frag && !sm->infos[i].compacting) {
            worst = &sm->infos[i];
            worst_frag = sm->infos[i].frag_ratio;
        }
    }
    return worst;
}

/* ── Extended segment management ─────────────────────────── */

/*
 * Get the total number of segments tracked.
 */
uint32_t pg_segment_mgr_count(const pg_segment_mgr *sm) {
    return sm->info_count;
}

/*
 * Reset all segment tracking info.
 */
void pg_segment_mgr_reset(pg_segment_mgr *sm) {
    for (uint32_t i = 0; i < sm->info_count; i++) {
        sm->infos[i].total_allocs = 0;
        sm->infos[i].total_frees = 0;
        sm->infos[i].frag_ratio = 0.0f;
        sm->infos[i].compacting = false;
    }
}

/*
 * Get the segment with the most live objects (for analysis).
 */
pg_segment_info *pg_segment_mgr_most_populated(pg_segment_mgr *sm) {
    pg_segment_info *best = NULL;
    uint32_t best_allocs = 0;
    for (uint32_t i = 0; i < sm->info_count; i++) {
        uint32_t live = sm->infos[i].total_allocs - sm->infos[i].total_frees;
        if (live > best_allocs) {
            best_allocs = live;
            best = &sm->infos[i];
        }
    }
    return best;
}

/*
 * Compute aggregate fragmentation across all segments.
 */
float pg_segment_mgr_avg_fragmentation(const pg_segment_mgr *sm) {
    if (sm->info_count == 0) return 0.0f;
    float total = 0.0f;
    for (uint32_t i = 0; i < sm->info_count; i++) {
        total += sm->infos[i].frag_ratio;
    }
    return total / (float)sm->info_count;
}

/*
 * Find segment info by segment ID.
 */
pg_segment_info *pg_segment_mgr_find_by_id(pg_segment_mgr *sm,
                                             uint32_t segment_id) {
    for (uint32_t i = 0; i < sm->info_count; i++) {
        if (sm->infos[i].arena_seg &&
            sm->infos[i].arena_seg->segment_id == segment_id) {
            return &sm->infos[i];
        }
    }
    return NULL;
}

/*
 * Remove tracking info for a segment.
 */
void pg_segment_mgr_untrack(pg_segment_mgr *sm, pg_arena_segment *seg) {
    for (uint32_t i = 0; i < sm->info_count; i++) {
        if (sm->infos[i].arena_seg == seg) {
            /* shift remaining entries */
            if (i < sm->info_count - 1) {
                memmove(&sm->infos[i], &sm->infos[i + 1],
                        (sm->info_count - i - 1) * sizeof(pg_segment_info));
            }
            sm->info_count--;
            return;
        }
    }
}

/*
 * Get the total number of allocations across all tracked segments.
 */
uint32_t pg_segment_mgr_total_allocs(const pg_segment_mgr *sm) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < sm->info_count; i++) {
        total += sm->infos[i].total_allocs;
    }
    return total;
}

/*
 * Get the total number of frees across all tracked segments.
 */
uint32_t pg_segment_mgr_total_frees(const pg_segment_mgr *sm) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < sm->info_count; i++) {
        total += sm->infos[i].total_frees;
    }
    return total;
}
