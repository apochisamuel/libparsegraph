#ifndef PG_HASH_H
#define PG_HASH_H

#include "parsegraph/types.h"

/*
 * Open-addressing hash table used by label/property/edge indexes.
 * Supports grow (rehash) when load factor exceeds threshold.
 *
 * BUG-6 SITE: hash_table_grow frees the old bucket array.
 * If an index scan cursor holds a pointer into the old array,
 * the scan reads stale memory after grow.
 */

typedef struct pg_hash_bucket {
    uint64_t key;
    void    *value;
    uint8_t  state;    /* 0=empty, 1=occupied, 2=tombstone */
} pg_hash_bucket;

typedef struct pg_hash_table {
    pg_hash_bucket *buckets;
    uint32_t        capacity;
    uint32_t        count;
    uint32_t        mask;
    uint32_t        tombstones;
} pg_hash_table;

pg_status pg_hash_init(pg_hash_table *ht, uint32_t initial_cap);
void      pg_hash_free(pg_hash_table *ht);
pg_status pg_hash_put(pg_hash_table *ht, uint64_t key, void *value);
void     *pg_hash_get(pg_hash_table *ht, uint64_t key);
void     *pg_hash_remove(pg_hash_table *ht, uint64_t key);
pg_status pg_hash_grow(pg_hash_table *ht);
uint32_t  pg_hash_count(const pg_hash_table *ht);
float     pg_hash_load_factor(const pg_hash_table *ht);

/* iteration — returns pointer into bucket array */
typedef struct pg_hash_iter {
    pg_hash_table  *table;
    uint32_t        pos;
    pg_hash_bucket *current_bucket;   /* cached pointer into buckets[] */
} pg_hash_iter;

void pg_hash_iter_init(pg_hash_iter *it, pg_hash_table *ht);
bool pg_hash_iter_next(pg_hash_iter *it, uint64_t *key, void **value);

#endif /* PG_HASH_H */
