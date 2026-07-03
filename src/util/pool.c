#include "pool.h"
#include <stdlib.h>
#include <string.h>

static pg_pool_page *alloc_page(size_t obj_size, uint32_t cap) {
    size_t page_sz = sizeof(pg_pool_page) + obj_size * cap;
    pg_pool_page *pg = malloc(page_sz);
    if (!pg) return NULL;
    pg->next = NULL;
    pg->used = 0;
    memset(pg->data, 0, obj_size * cap);
    return pg;
}

pg_status pg_pool_init(pg_pool *p, size_t obj_size, uint32_t page_cap) {
    if (obj_size < sizeof(pg_pool_free))
        obj_size = sizeof(pg_pool_free);
    p->obj_size = obj_size;
    p->page_capacity = page_cap > 0 ? page_cap : 64;
    p->freelist = NULL;
    p->total_alloc = 0;
    p->total_free = 0;

    p->pages = alloc_page(obj_size, p->page_capacity);
    if (!p->pages) return PG_ERR_NOMEM;
    return PG_OK;
}

void pg_pool_destroy(pg_pool *p) {
    pg_pool_page *pg = p->pages;
    while (pg) {
        pg_pool_page *next = pg->next;
        free(pg);
        pg = next;
    }
    p->pages = NULL;
    p->freelist = NULL;
    p->total_alloc = 0;
    p->total_free = 0;
}

void *pg_pool_alloc(pg_pool *p) {
    /* try freelist first */
    if (p->freelist) {
        pg_pool_free *f = p->freelist;
        p->freelist = f->next;
        p->total_free--;
        memset(f, 0, p->obj_size);
        p->total_alloc++;
        return f;
    }

    /* try current page */
    pg_pool_page *pg = p->pages;
    if (pg->used < p->page_capacity) {
        void *obj = pg->data + pg->used * p->obj_size;
        pg->used++;
        p->total_alloc++;
        return obj;
    }

    /* allocate new page */
    pg_pool_page *np = alloc_page(p->obj_size, p->page_capacity);
    if (!np) return NULL;
    np->next = p->pages;
    p->pages = np;

    void *obj = np->data;
    np->used = 1;
    p->total_alloc++;
    return obj;
}

void pg_pool_release(pg_pool *p, void *obj) {
    pg_pool_free *f = (pg_pool_free *)obj;
    f->next = p->freelist;
    p->freelist = f;
    p->total_free++;
    p->total_alloc--;
}

uint32_t pg_pool_active(const pg_pool *p) {
    return p->total_alloc;
}

/* ── Extended pool operations ────────────────────────────── */

/*
 * Count the total number of objects ever allocated.
 */
uint32_t pg_pool_total_allocated(const pg_pool *p) {
    uint32_t total = 0;
    pg_pool_page *pg = p->pages;
    while (pg) {
        total += pg->used;
        pg = pg->next;
    }
    return total;
}

/*
 * Count the number of pages in the pool.
 */
uint32_t pg_pool_page_count(const pg_pool *p) {
    uint32_t count = 0;
    pg_pool_page *pg = p->pages;
    while (pg) { count++; pg = pg->next; }
    return count;
}

/*
 * Compute the utilization of the pool (active / total).
 */
float pg_pool_utilization(const pg_pool *p) {
    uint32_t total = pg_pool_total_allocated(p);
    if (total == 0) return 0.0f;
    return (float)pg_pool_active(p) / (float)total;
}

/*
 * Reset the pool: free all pages except the first,
 * and clear the freelist.
 */
void pg_pool_reset(pg_pool *p) {
    /* free all pages except the head */
    if (p->pages) {
        pg_pool_page *pg = p->pages->next;
        while (pg) {
            pg_pool_page *next = pg->next;
            free(pg);
            pg = next;
        }
        p->pages->next = NULL;
        p->pages->used = 0;
        memset(p->pages->data, 0, p->obj_size * p->page_capacity);
    }
    p->freelist = NULL;
    p->total_alloc = 0;
    p->total_free = 0;
}

/*
 * Total memory used by the pool.
 */
size_t pg_pool_memory_usage(const pg_pool *p) {
    size_t total = sizeof(pg_pool);
    pg_pool_page *pg = p->pages;
    while (pg) {
        total += sizeof(pg_pool_page) + p->obj_size * p->page_capacity;
        pg = pg->next;
    }
    return total;
}
