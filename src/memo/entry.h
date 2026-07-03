#ifndef PG_ENTRY_H
#define PG_ENTRY_H

#include "parsegraph/types.h"
#include "../exec/matcher.h"

typedef struct pg_cache_entry {
    uint64_t         key;
    pg_match_result  result;
    pg_generation    stored_gen;
    uint32_t         access_count;
    uint32_t         lru_stamp;
    bool             valid;
} pg_cache_entry;

pg_cache_entry *pg_entry_create(uint64_t key, pg_match_result *result,
                                 pg_generation gen);
void            pg_entry_destroy(pg_cache_entry *entry);
pg_cache_entry *pg_entry_access(pg_cache_entry *entry);

#endif
