#include "eviction.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_eviction_init(pg_eviction_ctx *ec, uint32_t capacity) {
    ec->capacity = capacity;
    ec->entries = calloc(capacity, sizeof(pg_cache_entry *));
    if (!ec->entries) return PG_ERR_NOMEM;
    ec->count = 0;
    ec->clock = 0;
    return PG_OK;
}

void pg_eviction_free(pg_eviction_ctx *ec) {
    free(ec->entries);
    ec->entries = NULL;
    ec->count = 0;
}

pg_status pg_eviction_track(pg_eviction_ctx *ec, pg_cache_entry *entry) {
    if (ec->count >= ec->capacity) return PG_ERR_FULL;
    for (uint32_t i = 0; i < ec->capacity; i++) {
        if (!ec->entries[i]) {
            ec->entries[i] = entry;
            entry->lru_stamp = ec->clock++;
            ec->count++;
            return PG_OK;
        }
    }
    return PG_ERR_FULL;
}

pg_status pg_eviction_untrack(pg_eviction_ctx *ec, pg_cache_entry *entry) {
    for (uint32_t i = 0; i < ec->capacity; i++) {
        if (ec->entries[i] == entry) {
            ec->entries[i] = NULL;
            ec->count--;
            return PG_OK;
        }
    }
    return PG_ERR_NOT_FOUND;
}

/*
 * Find the entry with the lowest lru_stamp (least recently used).
 * BUG-8: this can select an entry that the executor currently holds
 * a pointer to via entry_access(), since there's no pin mechanism.
 */
pg_cache_entry *pg_eviction_find_victim(pg_eviction_ctx *ec) {
    pg_cache_entry *victim = NULL;
    uint32_t min_stamp = UINT32_MAX;

    for (uint32_t i = 0; i < ec->capacity; i++) {
        if (ec->entries[i] && ec->entries[i]->valid) {
            if (ec->entries[i]->lru_stamp < min_stamp) {
                min_stamp = ec->entries[i]->lru_stamp;
                victim = ec->entries[i];
            }
        }
    }
    return victim;
}

/*
 * BUG-8 FREE SITE:
 * Evicts the LRU entry, destroying it immediately.  If the executor
 * holds a pointer to this entry (returned by entry_access), the
 * pointer becomes dangling → heap-use-after-free.
 *
 * The missing abstraction: entry pinning with deferred eviction.
 * Pinned entries should be skipped during eviction.
 */
pg_status pg_eviction_evict(pg_eviction_ctx *ec) {
    pg_cache_entry *victim = pg_eviction_find_victim(ec);
    if (!victim) return PG_ERR_NOT_FOUND;

    pg_eviction_untrack(ec, victim);
    pg_entry_destroy(victim);                /* ← free site */
    return PG_OK;
}

/* ── Extended eviction policies ──────────────────────────── */

/*
 * Evict N entries at once, returning the number actually evicted.
 */
uint32_t pg_eviction_evict_n(pg_eviction_ctx *ec, uint32_t n) {
    uint32_t evicted = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (pg_eviction_evict(ec) == PG_OK) {
            evicted++;
        } else {
            break;
        }
    }
    return evicted;
}

/*
 * Find the entry with the highest access count.
 * Used by MFU (Most Frequently Used) analysis.
 */
pg_cache_entry *pg_eviction_find_hottest(pg_eviction_ctx *ec) {
    pg_cache_entry *hottest = NULL;
    uint32_t max_access = 0;

    for (uint32_t i = 0; i < ec->capacity; i++) {
        if (ec->entries[i] && ec->entries[i]->valid) {
            if (ec->entries[i]->access_count > max_access) {
                max_access = ec->entries[i]->access_count;
                hottest = ec->entries[i];
            }
        }
    }
    return hottest;
}

/*
 * Touch an entry to update its LRU stamp.
 */
void pg_eviction_touch(pg_eviction_ctx *ec, pg_cache_entry *entry) {
    entry->lru_stamp = ec->clock++;
}

/*
 * Get current eviction queue utilization.
 */
float pg_eviction_utilization(const pg_eviction_ctx *ec) {
    if (ec->capacity == 0) return 0.0f;
    return (float)ec->count / (float)ec->capacity;
}
