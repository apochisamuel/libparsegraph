#ifndef PG_SLAB_H
#define PG_SLAB_H

#include "parsegraph/types.h"

/*
 * Slab allocator — allocates chunks of a fixed size from contiguous
 * blocks.  Unlike pool.h, slabs support iteration over live objects
 * via a bitmap.
 */

#define PG_SLAB_BLOCK_OBJS 128

typedef struct pg_slab_block {
    struct pg_slab_block *next;
    uint32_t  bitmap[PG_SLAB_BLOCK_OBJS / 32];  /* 1 = allocated */
    uint32_t  live_count;
    uint8_t   data[];
} pg_slab_block;

typedef struct pg_slab {
    pg_slab_block *blocks;
    size_t         obj_size;
    uint32_t       total_live;
} pg_slab;

pg_status pg_slab_init(pg_slab *s, size_t obj_size);
void      pg_slab_destroy(pg_slab *s);
void     *pg_slab_alloc(pg_slab *s);
void      pg_slab_release(pg_slab *s, void *obj);
uint32_t  pg_slab_live_count(const pg_slab *s);

/* iteration */
typedef bool (*pg_slab_iter_fn)(void *obj, void *ctx);
void pg_slab_foreach(pg_slab *s, pg_slab_iter_fn fn, void *ctx);

#endif /* PG_SLAB_H */
