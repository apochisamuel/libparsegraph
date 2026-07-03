#ifndef PG_POOL_H
#define PG_POOL_H

#include "parsegraph/types.h"

/*
 * Fixed-size object pool backed by slab pages.
 * Each page holds N objects; freed objects are returned to a freelist.
 */

typedef struct pg_pool_page {
    struct pg_pool_page *next;
    uint32_t             used;
    uint8_t              data[];   /* flexible array of objects */
} pg_pool_page;

typedef struct pg_pool_free {
    struct pg_pool_free *next;
} pg_pool_free;

typedef struct pg_pool {
    pg_pool_page *pages;
    pg_pool_free *freelist;
    size_t        obj_size;
    uint32_t      page_capacity;
    uint32_t      total_alloc;
    uint32_t      total_free;
} pg_pool;

pg_status pg_pool_init(pg_pool *p, size_t obj_size, uint32_t page_cap);
void      pg_pool_destroy(pg_pool *p);
void     *pg_pool_alloc(pg_pool *p);
void      pg_pool_release(pg_pool *p, void *obj);
uint32_t  pg_pool_active(const pg_pool *p);

#endif /* PG_POOL_H */
