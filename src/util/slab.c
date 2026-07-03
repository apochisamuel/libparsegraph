#include "slab.h"
#include <stdlib.h>
#include <string.h>

static pg_slab_block *alloc_block(size_t obj_size) {
    size_t total = sizeof(pg_slab_block) + obj_size * PG_SLAB_BLOCK_OBJS;
    pg_slab_block *b = malloc(total);
    if (!b) return NULL;
    b->next = NULL;
    memset(b->bitmap, 0, sizeof(b->bitmap));
    b->live_count = 0;
    memset(b->data, 0, obj_size * PG_SLAB_BLOCK_OBJS);
    return b;
}

static inline bool bit_get(const uint32_t *bm, uint32_t idx) {
    return (bm[idx / 32] >> (idx % 32)) & 1;
}

static inline void bit_set(uint32_t *bm, uint32_t idx) {
    bm[idx / 32] |= (1u << (idx % 32));
}

static inline void bit_clear(uint32_t *bm, uint32_t idx) {
    bm[idx / 32] &= ~(1u << (idx % 32));
}

pg_status pg_slab_init(pg_slab *s, size_t obj_size) {
    if (obj_size < 8) obj_size = 8;
    s->obj_size = obj_size;
    s->total_live = 0;
    s->blocks = alloc_block(obj_size);
    if (!s->blocks) return PG_ERR_NOMEM;
    return PG_OK;
}

void pg_slab_destroy(pg_slab *s) {
    pg_slab_block *b = s->blocks;
    while (b) {
        pg_slab_block *next = b->next;
        free(b);
        b = next;
    }
    s->blocks = NULL;
    s->total_live = 0;
}

void *pg_slab_alloc(pg_slab *s) {
    /* find a block with space */
    pg_slab_block *b = s->blocks;
    while (b) {
        if (b->live_count < PG_SLAB_BLOCK_OBJS) {
            for (uint32_t i = 0; i < PG_SLAB_BLOCK_OBJS; i++) {
                if (!bit_get(b->bitmap, i)) {
                    bit_set(b->bitmap, i);
                    b->live_count++;
                    s->total_live++;
                    void *obj = b->data + i * s->obj_size;
                    memset(obj, 0, s->obj_size);
                    return obj;
                }
            }
        }
        b = b->next;
    }

    /* need a new block */
    pg_slab_block *nb = alloc_block(s->obj_size);
    if (!nb) return NULL;
    nb->next = s->blocks;
    s->blocks = nb;
    bit_set(nb->bitmap, 0);
    nb->live_count = 1;
    s->total_live++;
    return nb->data;
}

void pg_slab_release(pg_slab *s, void *obj) {
    pg_slab_block *b = s->blocks;
    while (b) {
        uint8_t *base = b->data;
        uint8_t *end = base + s->obj_size * PG_SLAB_BLOCK_OBJS;
        if ((uint8_t *)obj >= base && (uint8_t *)obj < end) {
            uint32_t idx = (uint32_t)((uint8_t *)obj - base) / s->obj_size;
            if (bit_get(b->bitmap, idx)) {
                bit_clear(b->bitmap, idx);
                b->live_count--;
                s->total_live--;
            }
            return;
        }
        b = b->next;
    }
}

uint32_t pg_slab_live_count(const pg_slab *s) {
    return s->total_live;
}

void pg_slab_foreach(pg_slab *s, pg_slab_iter_fn fn, void *ctx) {
    pg_slab_block *b = s->blocks;
    while (b) {
        for (uint32_t i = 0; i < PG_SLAB_BLOCK_OBJS; i++) {
            if (bit_get(b->bitmap, i)) {
                void *obj = b->data + i * s->obj_size;
                if (!fn(obj, ctx)) return;
            }
        }
        b = b->next;
    }
}

/* ── Extended slab operations ────────────────────────────── */

/*
 * Count the total number of blocks in the slab.
 */
uint32_t pg_slab_block_count(const pg_slab *s) {
    uint32_t count = 0;
    pg_slab_block *b = s->blocks;
    while (b) { count++; b = b->next; }
    return count;
}

/*
 * Total memory used by the slab.
 */
size_t pg_slab_memory_usage(const pg_slab *s) {
    size_t per_block = sizeof(pg_slab_block) +
                       s->obj_size * PG_SLAB_BLOCK_OBJS;
    return per_block * pg_slab_block_count(s);
}

/*
 * Utilization: live objects / total capacity.
 */
float pg_slab_utilization(const pg_slab *s) {
    uint32_t total_cap = pg_slab_block_count(s) * PG_SLAB_BLOCK_OBJS;
    if (total_cap == 0) return 0.0f;
    return (float)s->total_live / (float)total_cap;
}

/*
 * Compact the slab: if any block has zero live objects, free it.
 */
uint32_t pg_slab_compact(pg_slab *s) {
    uint32_t freed = 0;
    pg_slab_block **pp = &s->blocks;
    while (*pp) {
        if ((*pp)->live_count == 0 && (*pp)->next != NULL) {
            /* don't free the last block */
            pg_slab_block *dead = *pp;
            *pp = dead->next;
            free(dead);
            freed++;
        } else {
            pp = &(*pp)->next;
        }
    }
    return freed;
}

/*
 * Count objects matching a predicate.
 */
uint32_t pg_slab_count_if(pg_slab *s, pg_slab_iter_fn pred, void *ctx) {
    uint32_t count = 0;
    pg_slab_block *b = s->blocks;
    while (b) {
        for (uint32_t i = 0; i < PG_SLAB_BLOCK_OBJS; i++) {
            if (bit_get(b->bitmap, i)) {
                void *obj = b->data + i * s->obj_size;
                if (pred(obj, ctx)) count++;
            }
        }
        b = b->next;
    }
    return count;
}
