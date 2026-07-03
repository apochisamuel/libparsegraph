#include "page.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_page_mgr_init(pg_page_mgr *pm) {
    pm->pages = NULL;
    pm->page_count = 0;
    pm->next_id = 1;
    return PG_OK;
}

void pg_page_mgr_free(pg_page_mgr *pm) {
    pg_page *p = pm->pages;
    while (p) {
        pg_page *next = p->next;
        free(p);
        p = next;
    }
    pm->pages = NULL;
    pm->page_count = 0;
}

pg_page *pg_page_alloc(pg_page_mgr *pm) {
    pg_page *p = calloc(1, sizeof(pg_page));
    if (!p) return NULL;
    p->page_id = pm->next_id++;
    p->used = 0;
    p->next = pm->pages;
    pm->pages = p;
    pm->page_count++;
    return p;
}

void pg_page_release(pg_page_mgr *pm, pg_page *p) {
    pg_page **pp = &pm->pages;
    while (*pp) {
        if (*pp == p) {
            *pp = p->next;
            free(p);
            pm->page_count--;
            return;
        }
        pp = &(*pp)->next;
    }
}

void *pg_page_write(pg_page *p, const void *data, uint32_t len) {
    if (p->used + len > PG_PAGE_SIZE) return NULL;
    void *dest = p->data + p->used;
    memcpy(dest, data, len);
    p->used += len;
    return dest;
}

uint32_t pg_page_remaining(const pg_page *p) {
    return PG_PAGE_SIZE - p->used;
}

/* ── Extended page operations ────────────────────────────── */

/*
 * Get the page with the most free space.
 */
pg_page *pg_page_find_best_fit(pg_page_mgr *pm, uint32_t needed) {
    pg_page *best = NULL;
    uint32_t best_remaining = 0;

    pg_page *p = pm->pages;
    while (p) {
        uint32_t rem = pg_page_remaining(p);
        if (rem >= needed && rem > best_remaining) {
            best = p;
            best_remaining = rem;
        }
        p = p->next;
    }
    return best;
}

/*
 * Total bytes used across all pages.
 */
uint32_t pg_page_mgr_bytes_used(const pg_page_mgr *pm) {
    uint32_t total = 0;
    pg_page *p = pm->pages;
    while (p) {
        total += p->used;
        p = p->next;
    }
    return total;
}

/*
 * Total capacity across all pages.
 */
uint32_t pg_page_mgr_total_capacity(const pg_page_mgr *pm) {
    return pm->page_count * PG_PAGE_SIZE;
}

/*
 * Utilization ratio.
 */
float pg_page_mgr_utilization(const pg_page_mgr *pm) {
    uint32_t cap = pg_page_mgr_total_capacity(pm);
    if (cap == 0) return 0.0f;
    return (float)pg_page_mgr_bytes_used(pm) / (float)cap;
}

/*
 * Reset a page (clear its data and used counter).
 */
void pg_page_reset(pg_page *p) {
    memset(p->data, 0, PG_PAGE_SIZE);
    p->used = 0;
}

/*
 * Read data from a page at a given offset.
 */
bool pg_page_read(const pg_page *p, uint32_t offset, void *out,
                   uint32_t len) {
    if (offset + len > p->used) return false;
    memcpy(out, p->data + offset, len);
    return true;
}

/*
 * Copy data from one page to another.
 */
pg_status pg_page_copy(pg_page *dst, const pg_page *src) {
    if (src->used > PG_PAGE_SIZE) return PG_ERR_OVERFLOW;
    memcpy(dst->data, src->data, src->used);
    dst->used = src->used;
    return PG_OK;
}
