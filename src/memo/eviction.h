#ifndef PG_EVICTION_H
#define PG_EVICTION_H

#include "parsegraph/types.h"
#include "entry.h"

typedef struct pg_eviction_ctx {
    pg_cache_entry **entries;
    uint32_t         count;
    uint32_t         capacity;
    uint32_t         clock;
} pg_eviction_ctx;

pg_status pg_eviction_init(pg_eviction_ctx *ec, uint32_t capacity);
void      pg_eviction_free(pg_eviction_ctx *ec);
pg_status pg_eviction_track(pg_eviction_ctx *ec, pg_cache_entry *entry);
pg_status pg_eviction_untrack(pg_eviction_ctx *ec, pg_cache_entry *entry);
pg_cache_entry *pg_eviction_find_victim(pg_eviction_ctx *ec);
pg_status pg_eviction_evict(pg_eviction_ctx *ec);

#endif
