#ifndef PG_CACHE_H
#define PG_CACHE_H

#include "parsegraph/types.h"
#include "entry.h"
#include "eviction.h"
#include "../util/map.h"

struct pg_memo_cache {
    pg_map            entries;
    pg_eviction_ctx   eviction;
    uint32_t          max_entries;
    uint32_t          store_count;
    uint32_t          hit_count;
    uint32_t          miss_count;
};

pg_status       pg_cache_create(pg_memo_cache **out, uint32_t max_entries);
void            pg_cache_destroy(pg_memo_cache *cache);
pg_status       pg_cache_store(pg_memo_cache *cache, uint64_t key,
                                pg_match_result *result, pg_generation gen);
pg_cache_entry *pg_cache_lookup(pg_memo_cache *cache, uint64_t key);
void            pg_cache_invalidate(pg_memo_cache *cache, pg_generation gen);
float           pg_cache_hit_rate(const pg_memo_cache *cache);
uint32_t        pg_cache_valid_count(pg_memo_cache *cache);
void            pg_cache_reset_stats(pg_memo_cache *cache);

#endif
