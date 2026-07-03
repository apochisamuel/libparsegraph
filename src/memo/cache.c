#include "cache.h"
#include <stdlib.h>

pg_status pg_cache_create(pg_memo_cache **out, uint32_t max_entries) {
    pg_memo_cache *c = calloc(1, sizeof(pg_memo_cache));
    if (!c) return PG_ERR_NOMEM;
    pg_status s = pg_map_init(&c->entries, max_entries);
    if (s != PG_OK) { free(c); return s; }
    s = pg_eviction_init(&c->eviction, max_entries);
    if (s != PG_OK) { pg_map_free(&c->entries); free(c); return s; }
    c->max_entries = max_entries;
    c->store_count = 0;
    c->hit_count = 0;
    c->miss_count = 0;
    *out = c;
    return PG_OK;
}

static void destroy_entry_iter(uint64_t key, void *value, void *ctx) {
    (void)key; (void)ctx;
    pg_entry_destroy((pg_cache_entry *)value);
}

void pg_cache_destroy(pg_memo_cache *cache) {
    if (!cache) return;
    pg_map_foreach(&cache->entries, destroy_entry_iter, NULL);
    pg_map_free(&cache->entries);
    pg_eviction_free(&cache->eviction);
    free(cache);
}

/*
 * BUG-2 ALIAS SITE (through cache):
 * Stores a MatchResult containing raw pointers to graph nodes.
 * These pointers can become stale if the nodes are freed.
 *
 * BUG-8 TRIGGER:
 * store may trigger eviction if at capacity.  If the executor
 * is holding a pointer from a prior lookup (entry_access),
 * that entry could be the eviction victim.
 */
pg_status pg_cache_store(pg_memo_cache *cache, uint64_t key,
                          pg_match_result *result, pg_generation gen) {
    /* evict if at capacity */
    if (pg_map_count(&cache->entries) >= cache->max_entries) {
        pg_cache_entry *victim = pg_eviction_find_victim(&cache->eviction);
        if (victim) {
            pg_map_remove(&cache->entries, victim->key);
            pg_eviction_untrack(&cache->eviction, victim);
            pg_entry_destroy(victim);          /* BUG-8: may free
                                                   a held entry */
        }
    }

    pg_cache_entry *entry = pg_entry_create(key, result, gen);
    if (!entry) return PG_ERR_NOMEM;

    /* remove existing entry with same key if present */
    pg_cache_entry *old = pg_map_remove(&cache->entries, key);
    if (old) {
        pg_eviction_untrack(&cache->eviction, old);
        pg_entry_destroy(old);
    }

    pg_map_put(&cache->entries, key, entry);
    pg_eviction_track(&cache->eviction, entry);
    cache->store_count++;
    return PG_OK;
}

pg_cache_entry *pg_cache_lookup(pg_memo_cache *cache, uint64_t key) {
    pg_cache_entry *entry = pg_map_get(&cache->entries, key);
    if (entry && entry->valid) {
        cache->hit_count++;
        return pg_entry_access(entry);
    }
    cache->miss_count++;
    return NULL;
}

void pg_cache_invalidate(pg_memo_cache *cache, pg_generation gen) {
    /* simple: invalidate all entries older than gen */
    for (uint32_t i = 0; i < cache->entries.capacity; i++) {
        pg_map_entry *me = &cache->entries.buckets[i];
        if (me->occupied) {
            pg_cache_entry *ce = me->value;
            if (ce && ce->stored_gen < gen) {
                ce->valid = false;
            }
        }
    }
}

/* ── Extended cache operations ───────────────────────────── */

/*
 * Invalidate a specific entry by key.
 */
pg_status pg_cache_invalidate_key(pg_memo_cache *cache, uint64_t key) {
    pg_cache_entry *entry = pg_map_get(&cache->entries, key);
    if (!entry) return PG_ERR_NOT_FOUND;
    entry->valid = false;
    return PG_OK;
}

/*
 * Purge all invalid entries from the cache.
 * Frees memory held by invalidated entries.
 */
uint32_t pg_cache_purge_invalid(pg_memo_cache *cache) {
    uint32_t purged = 0;
    uint32_t cap = cache->entries.capacity;

    for (uint32_t i = 0; i < cap; i++) {
        pg_map_entry *me = &cache->entries.buckets[i];
        if (me->occupied) {
            pg_cache_entry *ce = me->value;
            if (ce && !ce->valid) {
                pg_eviction_untrack(&cache->eviction, ce);
                pg_entry_destroy(ce);
                me->value = NULL;
                me->occupied = 0;
                cache->entries.count--;
                purged++;
            }
        }
    }
    return purged;
}

/*
 * Cache hit rate as a float between 0 and 1.
 */
float pg_cache_hit_rate(const pg_memo_cache *cache) {
    uint32_t total = cache->hit_count + cache->miss_count;
    if (total == 0) return 0.0f;
    return (float)cache->hit_count / (float)total;
}

/*
 * Number of valid entries currently in the cache.
 */
uint32_t pg_cache_valid_count(pg_memo_cache *cache) {
    uint32_t count = 0;
    uint32_t cap = cache->entries.capacity;
    for (uint32_t i = 0; i < cap; i++) {
        pg_map_entry *me = &cache->entries.buckets[i];
        if (me->occupied) {
            pg_cache_entry *ce = me->value;
            if (ce && ce->valid) count++;
        }
    }
    return count;
}

/*
 * Reset cache statistics.
 */
void pg_cache_reset_stats(pg_memo_cache *cache) {
    cache->hit_count = 0;
    cache->miss_count = 0;
}
