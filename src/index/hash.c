#include "hash.h"
#include <stdlib.h>
#include <string.h>

static uint32_t hash_key(uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return (uint32_t)key;
}

static uint32_t npow2(uint32_t v) {
    v--; v |= v >> 1; v |= v >> 2;
    v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

pg_status pg_hash_init(pg_hash_table *ht, uint32_t initial_cap) {
    if (initial_cap < 16) initial_cap = 16;
    initial_cap = npow2(initial_cap);
    ht->buckets = calloc(initial_cap, sizeof(pg_hash_bucket));
    if (!ht->buckets) return PG_ERR_NOMEM;
    ht->capacity = initial_cap;
    ht->count = 0;
    ht->mask = initial_cap - 1;
    ht->tombstones = 0;
    return PG_OK;
}

void pg_hash_free(pg_hash_table *ht) {
    free(ht->buckets);
    ht->buckets = NULL;
    ht->capacity = 0;
    ht->count = 0;
}

static pg_hash_bucket *probe(pg_hash_table *ht, uint64_t key) {
    uint32_t idx = hash_key(key) & ht->mask;
    for (uint32_t i = 0; i < ht->capacity; i++) {
        uint32_t pos = (idx + i) & ht->mask;
        pg_hash_bucket *b = &ht->buckets[pos];
        if (b->state == 0) return b;          /* empty */
        if (b->state == 1 && b->key == key) return b;
    }
    return NULL;
}

/*
 * BUG-6 FREE SITE:
 * grow() allocates a new bucket array and frees the old one.
 * If a label_index scan cursor (pg_hash_iter) has cached a
 * pointer into the old bucket array (current_bucket), iterating
 * after grow reads freed memory → heap-buffer-overflow.
 *
 * The missing abstraction is a HashGeneration counter that
 * scan cursors capture and check before accessing buckets.
 */
pg_status pg_hash_grow(pg_hash_table *ht) {
    uint32_t old_cap = ht->capacity;
    pg_hash_bucket *old_buckets = ht->buckets;     /* will be freed */

    uint32_t new_cap = old_cap * 2;
    ht->buckets = calloc(new_cap, sizeof(pg_hash_bucket));
    if (!ht->buckets) {
        ht->buckets = old_buckets;
        return PG_ERR_NOMEM;
    }
    ht->capacity = new_cap;
    ht->mask = new_cap - 1;
    ht->count = 0;
    ht->tombstones = 0;

    for (uint32_t i = 0; i < old_cap; i++) {
        if (old_buckets[i].state == 1) {
            pg_hash_put(ht, old_buckets[i].key, old_buckets[i].value);
        }
    }

    free(old_buckets);                             /* ← free site */
    return PG_OK;
}

pg_status pg_hash_put(pg_hash_table *ht, uint64_t key, void *value) {
    float lf = (float)(ht->count + ht->tombstones) / (float)ht->capacity;
    if (lf > PG_HASH_LOAD_FACTOR) {
        pg_status s = pg_hash_grow(ht);
        if (s != PG_OK) return s;
    }

    pg_hash_bucket *b = probe(ht, key);
    if (!b) return PG_ERR_FULL;
    if (b->state != 1) {
        b->key = key;
        b->state = 1;
        ht->count++;
    }
    b->value = value;
    return PG_OK;
}

void *pg_hash_get(pg_hash_table *ht, uint64_t key) {
    pg_hash_bucket *b = probe(ht, key);
    if (b && b->state == 1 && b->key == key) return b->value;
    return NULL;
}

void *pg_hash_remove(pg_hash_table *ht, uint64_t key) {
    pg_hash_bucket *b = probe(ht, key);
    if (!b || b->state != 1 || b->key != key) return NULL;
    void *val = b->value;
    b->state = 2;   /* tombstone */
    b->value = NULL;
    ht->count--;
    ht->tombstones++;
    return val;
}

uint32_t pg_hash_count(const pg_hash_table *ht) { return ht->count; }

float pg_hash_load_factor(const pg_hash_table *ht) {
    if (ht->capacity == 0) return 0.0f;
    return (float)ht->count / (float)ht->capacity;
}

void pg_hash_iter_init(pg_hash_iter *it, pg_hash_table *ht) {
    it->table = ht;
    it->pos = 0;
    it->current_bucket = NULL;
}

bool pg_hash_iter_next(pg_hash_iter *it, uint64_t *key, void **value) {
    while (it->pos < it->table->capacity) {
        pg_hash_bucket *b = &it->table->buckets[it->pos];
        it->pos++;
        if (b->state == 1) {
            it->current_bucket = b;    /* cache pointer into array */
            *key = b->key;
            *value = b->value;
            return true;
        }
    }
    return false;
}

/* ── Extended hash operations ────────────────────────────── */

/*
 * Clear all entries without freeing the bucket array.
 * Preserves capacity for reuse.
 */
void pg_hash_clear(pg_hash_table *ht) {
    memset(ht->buckets, 0, ht->capacity * sizeof(pg_hash_bucket));
    ht->count = 0;
    ht->tombstones = 0;
}

/*
 * Compact the hash table by removing tombstones.
 * Creates a new array and re-inserts all live entries.
 */
pg_status pg_hash_compact(pg_hash_table *ht) {
    if (ht->tombstones == 0) return PG_OK;

    uint32_t old_cap = ht->capacity;
    pg_hash_bucket *old = ht->buckets;

    ht->buckets = calloc(old_cap, sizeof(pg_hash_bucket));
    if (!ht->buckets) {
        ht->buckets = old;
        return PG_ERR_NOMEM;
    }
    ht->count = 0;
    ht->tombstones = 0;

    for (uint32_t i = 0; i < old_cap; i++) {
        if (old[i].state == 1) {
            pg_hash_put(ht, old[i].key, old[i].value);
        }
    }
    free(old);
    return PG_OK;
}

/*
 * Get all keys as an array. Caller must free the returned array.
 */
uint64_t *pg_hash_keys(pg_hash_table *ht, uint32_t *out_count) {
    uint64_t *keys = malloc(ht->count * sizeof(uint64_t));
    if (!keys) { *out_count = 0; return NULL; }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < ht->capacity && idx < ht->count; i++) {
        if (ht->buckets[i].state == 1) {
            keys[idx++] = ht->buckets[i].key;
        }
    }
    *out_count = idx;
    return keys;
}

/*
 * Check if the hash table needs compaction (high tombstone ratio).
 */
bool pg_hash_needs_compact(const pg_hash_table *ht) {
    if (ht->capacity == 0) return false;
    float tomb_ratio = (float)ht->tombstones / (float)ht->capacity;
    return tomb_ratio > 0.3f;
}

/*
 * Merge entries from another hash table into this one.
 * Does not take ownership of values.
 */
pg_status pg_hash_merge(pg_hash_table *dst, pg_hash_table *src) {
    pg_hash_iter it;
    pg_hash_iter_init(&it, src);
    uint64_t key;
    void *value;
    while (pg_hash_iter_next(&it, &key, &value)) {
        pg_status s = pg_hash_put(dst, key, value);
        if (s != PG_OK) return s;
    }
    return PG_OK;
}
