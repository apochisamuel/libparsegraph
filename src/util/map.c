#include "map.h"
#include <stdlib.h>
#include <string.h>

static uint32_t next_pow2(uint32_t v) {
    v--;
    v |= v >> 1;  v |= v >> 2;
    v |= v >> 4;  v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

static uint32_t hash64(uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return (uint32_t)key;
}

pg_status pg_map_init(pg_map *m, uint32_t initial_cap) {
    if (initial_cap < 16) initial_cap = 16;
    initial_cap = next_pow2(initial_cap);
    m->buckets = calloc(initial_cap, sizeof(pg_map_entry));
    if (!m->buckets) return PG_ERR_NOMEM;
    m->capacity = initial_cap;
    m->count = 0;
    m->mask = initial_cap - 1;
    return PG_OK;
}

void pg_map_free(pg_map *m) {
    free(m->buckets);
    m->buckets = NULL;
    m->capacity = 0;
    m->count = 0;
}

static pg_map_entry *find_slot(pg_map *m, uint64_t key) {
    uint32_t idx = hash64(key) & m->mask;
    for (uint32_t i = 0; i < m->capacity; i++) {
        uint32_t probe = (idx + i) & m->mask;
        pg_map_entry *e = &m->buckets[probe];
        if (!e->occupied || e->key == key) return e;
    }
    return NULL;
}

pg_status pg_map_grow(pg_map *m) {
    uint32_t old_cap = m->capacity;
    pg_map_entry *old = m->buckets;

    uint32_t new_cap = old_cap * 2;
    m->buckets = calloc(new_cap, sizeof(pg_map_entry));
    if (!m->buckets) {
        m->buckets = old;
        return PG_ERR_NOMEM;
    }
    m->capacity = new_cap;
    m->mask = new_cap - 1;
    m->count = 0;

    for (uint32_t i = 0; i < old_cap; i++) {
        if (old[i].occupied) {
            pg_map_put(m, old[i].key, old[i].value);
        }
    }
    free(old);
    return PG_OK;
}

pg_status pg_map_put(pg_map *m, uint64_t key, void *value) {
    if (m->count * 4 >= m->capacity * 3) {
        pg_status s = pg_map_grow(m);
        if (s != PG_OK) return s;
    }
    pg_map_entry *e = find_slot(m, key);
    if (!e) return PG_ERR_FULL;
    if (!e->occupied) {
        e->key = key;
        e->occupied = 1;
        m->count++;
    }
    e->value = value;
    return PG_OK;
}

void *pg_map_get(pg_map *m, uint64_t key) {
    pg_map_entry *e = find_slot(m, key);
    if (e && e->occupied && e->key == key) return e->value;
    return NULL;
}

bool pg_map_has(pg_map *m, uint64_t key) {
    pg_map_entry *e = find_slot(m, key);
    return e && e->occupied && e->key == key;
}

void *pg_map_remove(pg_map *m, uint64_t key) {
    pg_map_entry *e = find_slot(m, key);
    if (!e || !e->occupied || e->key != key) return NULL;
    void *val = e->value;
    e->occupied = 0;
    e->value = NULL;
    m->count--;

    /* re-insert displaced entries */
    uint32_t idx = (uint32_t)(e - m->buckets);
    uint32_t j = (idx + 1) & m->mask;
    while (m->buckets[j].occupied) {
        pg_map_entry tmp = m->buckets[j];
        m->buckets[j].occupied = 0;
        m->count--;
        pg_map_put(m, tmp.key, tmp.value);
        j = (j + 1) & m->mask;
    }
    return val;
}

uint32_t pg_map_count(const pg_map *m) {
    return m->count;
}

void pg_map_foreach(pg_map *m, pg_map_iter_fn fn, void *ctx) {
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (m->buckets[i].occupied) {
            fn(m->buckets[i].key, m->buckets[i].value, ctx);
        }
    }
}

/* ── Extended map operations ─────────────────────────────── */

/*
 * Get all keys as an array. Caller must free.
 */
uint64_t *pg_map_keys(pg_map *m, uint32_t *out_count) {
    uint64_t *keys = malloc(m->count * sizeof(uint64_t));
    if (!keys) { *out_count = 0; return NULL; }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (m->buckets[i].occupied) {
            keys[idx++] = m->buckets[i].key;
        }
    }
    *out_count = idx;
    return keys;
}

/*
 * Get all values as an array. Caller must free.
 */
void **pg_map_values(pg_map *m, uint32_t *out_count) {
    void **vals = malloc(m->count * sizeof(void *));
    if (!vals) { *out_count = 0; return NULL; }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (m->buckets[i].occupied) {
            vals[idx++] = m->buckets[i].value;
        }
    }
    *out_count = idx;
    return vals;
}

/*
 * Clear all entries.
 */
void pg_map_clear(pg_map *m) {
    memset(m->buckets, 0, m->capacity * sizeof(pg_map_entry));
    m->count = 0;
}

/*
 * Put if absent: only inserts if the key doesn't exist.
 * Returns true if inserted, false if key already existed.
 */
bool pg_map_put_if_absent(pg_map *m, uint64_t key, void *value) {
    if (pg_map_has(m, key)) return false;
    pg_map_put(m, key, value);
    return true;
}

/*
 * Compute the load factor.
 */
float pg_map_load_factor(const pg_map *m) {
    if (m->capacity == 0) return 0.0f;
    return (float)m->count / (float)m->capacity;
}
