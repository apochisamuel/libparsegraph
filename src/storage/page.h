#ifndef PG_PAGE_H
#define PG_PAGE_H

#include "parsegraph/types.h"

#define PG_PAGE_SIZE 4096

typedef struct pg_page {
    uint8_t        data[PG_PAGE_SIZE];
    uint32_t       used;
    uint32_t       page_id;
    struct pg_page *next;
} pg_page;

typedef struct pg_page_mgr {
    pg_page  *pages;
    uint32_t  page_count;
    uint32_t  next_id;
} pg_page_mgr;

pg_status  pg_page_mgr_init(pg_page_mgr *pm);
void       pg_page_mgr_free(pg_page_mgr *pm);
pg_page   *pg_page_alloc(pg_page_mgr *pm);
void       pg_page_release(pg_page_mgr *pm, pg_page *p);
void      *pg_page_write(pg_page *p, const void *data, uint32_t len);
uint32_t   pg_page_remaining(const pg_page *p);

#endif /* PG_PAGE_H */
