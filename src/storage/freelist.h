#ifndef PG_FREELIST_H
#define PG_FREELIST_H

#include "parsegraph/types.h"

typedef struct pg_freelist_entry {
    uint32_t slot_idx;
    struct pg_freelist_entry *next;
} pg_freelist_entry;

typedef struct pg_freelist {
    pg_freelist_entry *head;
    uint32_t           count;
} pg_freelist;

pg_status pg_freelist_init(pg_freelist *fl);
void      pg_freelist_free(pg_freelist *fl);
pg_status pg_freelist_push(pg_freelist *fl, uint32_t slot_idx);
bool      pg_freelist_pop(pg_freelist *fl, uint32_t *out);
uint32_t  pg_freelist_count(const pg_freelist *fl);

#endif /* PG_FREELIST_H */
