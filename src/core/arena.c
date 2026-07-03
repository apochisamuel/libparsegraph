#include "arena.h"
#include <stdlib.h>
#include <string.h>

static pg_arena_segment *create_segment(uint32_t slot_size, uint32_t cap,
                                         uint32_t id) {
    pg_arena_segment *seg = calloc(1, sizeof(pg_arena_segment));
    if (!seg) return NULL;
    seg->data = malloc((size_t)slot_size * cap);
    if (!seg->data) { free(seg); return NULL; }
    seg->occupied = calloc(cap, 1);
    if (!seg->occupied) { free(seg->data); free(seg); return NULL; }
    seg->slot_size = slot_size;
    seg->slot_count = cap;
    seg->used = 0;
    seg->live = 0;
    seg->segment_id = id;
    seg->next = NULL;
    memset(seg->data, 0, (size_t)slot_size * cap);
    return seg;
}

pg_status pg_arena_init(pg_arena *a, uint32_t slot_size, uint32_t segment_cap) {
    a->slot_size = slot_size;
    a->segment_capacity = segment_cap;
    a->next_segment_id = 1;
    a->total_live = 0;
    a->total_slots = 0;

    a->segments = create_segment(slot_size, segment_cap, a->next_segment_id++);
    if (!a->segments) return PG_ERR_NOMEM;
    a->total_slots = segment_cap;
    return PG_OK;
}

void pg_arena_destroy(pg_arena *a) {
    pg_arena_segment *s = a->segments;
    while (s) {
        pg_arena_segment *next = s->next;
        free(s->data);
        free(s->occupied);
        free(s);
        s = next;
    }
    a->segments = NULL;
    a->total_live = 0;
    a->total_slots = 0;
}

void *pg_arena_alloc(pg_arena *a) {
    /* try head segment first */
    pg_arena_segment *seg = a->segments;
    while (seg) {
        if (seg->used < seg->slot_count) {
            /* find an unoccupied slot */
            for (uint32_t i = 0; i < seg->slot_count; i++) {
                if (!seg->occupied[i]) {
                    seg->occupied[i] = 1;
                    seg->used++;
                    seg->live++;
                    a->total_live++;
                    void *ptr = seg->data + (size_t)i * seg->slot_size;
                    memset(ptr, 0, seg->slot_size);
                    return ptr;
                }
            }
        }
        seg = seg->next;
    }

    /* all segments full — create a new one */
    pg_arena_segment *ns = create_segment(a->slot_size, a->segment_capacity,
                                           a->next_segment_id++);
    if (!ns) return NULL;
    ns->next = a->segments;
    a->segments = ns;
    a->total_slots += a->segment_capacity;

    ns->occupied[0] = 1;
    ns->used = 1;
    ns->live = 1;
    a->total_live++;
    memset(ns->data, 0, a->slot_size);
    return ns->data;
}

void pg_arena_free_slot(pg_arena *a, void *ptr) {
    pg_arena_segment *seg = a->segments;
    while (seg) {
        uint8_t *base = seg->data;
        uint8_t *end = base + (size_t)seg->slot_size * seg->slot_count;
        if ((uint8_t *)ptr >= base && (uint8_t *)ptr < end) {
            uint32_t idx = (uint32_t)((uint8_t *)ptr - base) / seg->slot_size;
            if (seg->occupied[idx]) {
                seg->occupied[idx] = 0;
                seg->live--;
                a->total_live--;
            }
            return;
        }
        seg = seg->next;
    }
}

float pg_arena_utilization(const pg_arena *a) {
    if (a->total_slots == 0) return 1.0f;
    return (float)a->total_live / (float)a->total_slots;
}

pg_arena_segment *pg_arena_find_segment(pg_arena *a, void *ptr) {
    pg_arena_segment *seg = a->segments;
    while (seg) {
        uint8_t *base = seg->data;
        uint8_t *end = base + (size_t)seg->slot_size * seg->slot_count;
        if ((uint8_t *)ptr >= base && (uint8_t *)ptr < end)
            return seg;
        seg = seg->next;
    }
    return NULL;
}

pg_arena_segment *pg_arena_active_segment(pg_arena *a) {
    return a->segments;
}

pg_arena_segment *pg_arena_new_segment(pg_arena *a) {
    pg_arena_segment *ns = create_segment(a->slot_size, a->segment_capacity,
                                           a->next_segment_id++);
    if (!ns) return NULL;
    ns->next = a->segments;
    a->segments = ns;
    a->total_slots += a->segment_capacity;
    return ns;
}

void pg_arena_release_segment(pg_arena *a, pg_arena_segment *seg) {
    /* unlink from list */
    pg_arena_segment **pp = &a->segments;
    while (*pp) {
        if (*pp == seg) {
            *pp = seg->next;
            break;
        }
        pp = &(*pp)->next;
    }
    a->total_slots -= seg->slot_count;
    a->total_live -= seg->live;
    free(seg->data);
    free(seg->occupied);
    free(seg);
}

/* ── Extended arena operations ───────────────────────────── */

/*
 * Count the total number of segments in the arena.
 */
uint32_t pg_arena_segment_count(const pg_arena *a) {
    uint32_t count = 0;
    pg_arena_segment *s = a->segments;
    while (s) { count++; s = s->next; }
    return count;
}

/*
 * Get the segment at a given index (0-based).
 */
pg_arena_segment *pg_arena_segment_at(pg_arena *a, uint32_t idx) {
    pg_arena_segment *s = a->segments;
    for (uint32_t i = 0; i < idx && s; i++) s = s->next;
    return s;
}

/*
 * Iterate all live objects in the arena.
 */
typedef bool (*pg_arena_iter_fn)(void *obj, uint32_t idx, void *ctx);

void pg_arena_foreach(pg_arena *a, pg_arena_iter_fn fn, void *ctx) {
    pg_arena_segment *s = a->segments;
    while (s) {
        for (uint32_t i = 0; i < s->slot_count; i++) {
            if (s->occupied[i]) {
                void *obj = s->data + (size_t)i * s->slot_size;
                if (!fn(obj, i, ctx)) return;
            }
        }
        s = s->next;
    }
}

/*
 * Compute fragmentation ratio for a specific segment.
 */
float pg_arena_segment_fragmentation(pg_arena_segment *seg) {
    if (seg->used == 0) return 0.0f;
    return 1.0f - (float)seg->live / (float)seg->used;
}

/*
 * Total memory used by the arena (all segments combined).
 */
size_t pg_arena_total_memory(const pg_arena *a) {
    size_t total = 0;
    pg_arena_segment *s = a->segments;
    while (s) {
        total += sizeof(pg_arena_segment);
        total += (size_t)s->slot_size * s->slot_count;
        total += s->slot_count;  /* occupied bitmap */
        s = s->next;
    }
    return total;
}

/*
 * Find the first free slot in any segment.
 * Returns the segment and slot index, or NULL if all full.
 */
pg_arena_segment *pg_arena_find_free(pg_arena *a, uint32_t *slot_idx) {
    pg_arena_segment *s = a->segments;
    while (s) {
        for (uint32_t i = 0; i < s->slot_count; i++) {
            if (!s->occupied[i]) {
                *slot_idx = i;
                return s;
            }
        }
        s = s->next;
    }
    return NULL;
}
