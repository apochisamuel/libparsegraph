#include "string_intern.h"
#include <stdlib.h>
#include <string.h>

static uint32_t fnv1a(const char *s, uint32_t len) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

pg_status pg_intern_init(pg_string_intern *si, uint32_t initial_buckets) {
    if (initial_buckets < 32) initial_buckets = 32;
    si->buckets = calloc(initial_buckets, sizeof(pg_intern_entry *));
    if (!si->buckets) return PG_ERR_NOMEM;
    si->bucket_count = initial_buckets;
    si->count = 0;
    si->next_id = 1;
    return PG_OK;
}

void pg_intern_free(pg_string_intern *si) {
    for (uint32_t i = 0; i < si->bucket_count; i++) {
        pg_intern_entry *e = si->buckets[i];
        while (e) {
            pg_intern_entry *next = e->next;
            free(e->str);
            free(e);
            e = next;
        }
    }
    free(si->buckets);
    si->buckets = NULL;
    si->count = 0;
}

static pg_intern_entry *find_entry(pg_string_intern *si, const char *str,
                                    uint32_t len, uint32_t h) {
    uint32_t idx = h % si->bucket_count;
    pg_intern_entry *e = si->buckets[idx];
    while (e) {
        if (e->hash == h && e->len == len && memcmp(e->str, str, len) == 0)
            return e;
        e = e->next;
    }
    return NULL;
}

static void rehash(pg_string_intern *si) {
    uint32_t new_count = si->bucket_count * 2;
    pg_intern_entry **nb = calloc(new_count, sizeof(pg_intern_entry *));
    if (!nb) return;

    for (uint32_t i = 0; i < si->bucket_count; i++) {
        pg_intern_entry *e = si->buckets[i];
        while (e) {
            pg_intern_entry *next = e->next;
            uint32_t idx = e->hash % new_count;
            e->next = nb[idx];
            nb[idx] = e;
            e = next;
        }
    }
    free(si->buckets);
    si->buckets = nb;
    si->bucket_count = new_count;
}

uint32_t pg_intern_put(pg_string_intern *si, const char *str, uint32_t len) {
    uint32_t h = fnv1a(str, len);
    pg_intern_entry *e = find_entry(si, str, len, h);
    if (e) return e->id;

    if (si->count >= si->bucket_count) rehash(si);

    e = malloc(sizeof(pg_intern_entry));
    if (!e) return 0;
    e->str = malloc(len + 1);
    if (!e->str) { free(e); return 0; }
    memcpy(e->str, str, len);
    e->str[len] = '\0';
    e->len = len;
    e->hash = h;
    e->id = si->next_id++;

    uint32_t idx = h % si->bucket_count;
    e->next = si->buckets[idx];
    si->buckets[idx] = e;
    si->count++;
    return e->id;
}

const char *pg_intern_get(pg_string_intern *si, uint32_t id) {
    for (uint32_t i = 0; i < si->bucket_count; i++) {
        pg_intern_entry *e = si->buckets[i];
        while (e) {
            if (e->id == id) return e->str;
            e = e->next;
        }
    }
    return NULL;
}

uint32_t pg_intern_find(pg_string_intern *si, const char *str, uint32_t len) {
    uint32_t h = fnv1a(str, len);
    pg_intern_entry *e = find_entry(si, str, len, h);
    return e ? e->id : 0;
}

/* ── Extended string intern operations ───────────────────── */

/*
 * Count the total number of interned strings.
 */
uint32_t pg_intern_count(const pg_string_intern *si) {
    return si->count;
}

/*
 * Iterate all interned strings.
 */
typedef bool (*pg_intern_iter_fn)(uint32_t id, const char *str,
                                   uint32_t len, void *ctx);

void pg_intern_foreach(pg_string_intern *si, pg_intern_iter_fn fn,
                        void *ctx) {
    for (uint32_t i = 0; i < si->bucket_count; i++) {
        pg_intern_entry *e = si->buckets[i];
        while (e) {
            if (!fn(e->id, e->str, e->len, ctx)) return;
            e = e->next;
        }
    }
}

/*
 * Total memory used by all interned strings.
 */
size_t pg_intern_memory_usage(const pg_string_intern *si) {
    size_t total = si->bucket_count * sizeof(pg_intern_entry *);
    for (uint32_t i = 0; i < si->bucket_count; i++) {
        pg_intern_entry *e = si->buckets[i];
        while (e) {
            total += sizeof(pg_intern_entry) + e->len + 1;
            e = e->next;
        }
    }
    return total;
}

/*
 * Check if a string has been interned.
 */
bool pg_intern_contains(pg_string_intern *si, const char *str,
                          uint32_t len) {
    return pg_intern_find(si, str, len) != 0;
}
