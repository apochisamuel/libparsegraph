#include "properties.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_props_init(pg_prop_store *ps) {
    ps->entries = NULL;
    ps->count = 0;
    ps->capacity = 0;
    return PG_OK;
}

void pg_value_free(pg_value *v) {
    if (v->type == PG_VAL_STRING) {
        free(v->v.str.data);
        v->v.str.data = NULL;
        v->v.str.len = 0;
    } else if (v->type == PG_VAL_BLOB) {
        free(v->v.blob.data);
        v->v.blob.data = NULL;
        v->v.blob.len = 0;
    }
    v->type = PG_VAL_NONE;
}

void pg_props_free(pg_prop_store *ps) {
    for (uint32_t i = 0; i < ps->count; i++) {
        pg_value_free(&ps->entries[i].value);
    }
    free(ps->entries);
    ps->entries = NULL;
    ps->count = 0;
    ps->capacity = 0;
}

pg_status pg_value_copy(pg_value *dst, const pg_value *src) {
    *dst = *src;
    if (src->type == PG_VAL_STRING && src->v.str.data) {
        dst->v.str.data = malloc(src->v.str.len + 1);
        if (!dst->v.str.data) return PG_ERR_NOMEM;
        memcpy(dst->v.str.data, src->v.str.data, src->v.str.len);
        dst->v.str.data[src->v.str.len] = '\0';
        dst->v.str.len = src->v.str.len;
    } else if (src->type == PG_VAL_BLOB && src->v.blob.data) {
        dst->v.blob.data = malloc(src->v.blob.len);
        if (!dst->v.blob.data) return PG_ERR_NOMEM;
        memcpy(dst->v.blob.data, src->v.blob.data, src->v.blob.len);
        dst->v.blob.len = src->v.blob.len;
    }
    return PG_OK;
}

pg_value *pg_props_get(pg_prop_store *ps, pg_prop_key key) {
    for (uint32_t i = 0; i < ps->count; i++) {
        if (ps->entries[i].key == key) {
            return &ps->entries[i].value;
        }
    }
    return NULL;
}

/*
 * BUG-5 FREE SITE:
 * When overwriting a property, we free the old value's heap storage
 * immediately.  If a filter stage in the executor pipeline has already
 * cached a pointer to this PropertyValue via filter_read_property(),
 * the pointer becomes stale.  A later aggregate_accumulate() that
 * dereferences the cached pointer will hit a use-after-free.
 *
 * The missing abstraction is a property-read pin that prevents
 * freeing a value while pipeline stages hold references to it.
 */
pg_status pg_props_set(pg_prop_store *ps, pg_prop_key key,
                        const pg_value *val) {
    /* overwrite existing? */
    for (uint32_t i = 0; i < ps->count; i++) {
        if (ps->entries[i].key == key) {
            pg_value_free(&ps->entries[i].value);       /* ← free site */
            return pg_value_copy(&ps->entries[i].value, val);
        }
    }

    /* insert new */
    if (ps->count >= ps->capacity) {
        uint32_t nc = ps->capacity == 0 ? PG_MAX_PROPS_INIT
                                        : ps->capacity * 2;
        pg_prop_entry *ne = realloc(ps->entries, nc * sizeof(pg_prop_entry));
        if (!ne) return PG_ERR_NOMEM;
        ps->entries = ne;
        ps->capacity = nc;
    }

    ps->entries[ps->count].key = key;
    pg_status s = pg_value_copy(&ps->entries[ps->count].value, val);
    if (s != PG_OK) return s;
    ps->count++;
    return PG_OK;
}

pg_status pg_props_remove(pg_prop_store *ps, pg_prop_key key) {
    for (uint32_t i = 0; i < ps->count; i++) {
        if (ps->entries[i].key == key) {
            pg_value_free(&ps->entries[i].value);
            if (i < ps->count - 1) {
                ps->entries[i] = ps->entries[ps->count - 1];
            }
            ps->count--;
            return PG_OK;
        }
    }
    return PG_ERR_NOT_FOUND;
}

uint32_t pg_props_count(const pg_prop_store *ps) {
    return ps->count;
}

/* ── Extended property operations ────────────────────────── */

/*
 * Check if a property key exists in the store.
 */
bool pg_props_has(const pg_prop_store *ps, pg_prop_key key) {
    for (uint32_t i = 0; i < ps->count; i++) {
        if (ps->entries[i].key == key) return true;
    }
    return false;
}

/*
 * Get all property keys as an array. Caller must free.
 */
pg_prop_key *pg_props_keys(const pg_prop_store *ps, uint32_t *out_count) {
    if (ps->count == 0) { *out_count = 0; return NULL; }
    pg_prop_key *keys = malloc(ps->count * sizeof(pg_prop_key));
    if (!keys) { *out_count = 0; return NULL; }
    for (uint32_t i = 0; i < ps->count; i++) {
        keys[i] = ps->entries[i].key;
    }
    *out_count = ps->count;
    return keys;
}

/*
 * Deep copy all properties from one store to another.
 */
pg_status pg_props_copy(pg_prop_store *dst, const pg_prop_store *src) {
    for (uint32_t i = 0; i < src->count; i++) {
        pg_status s = pg_props_set(dst, src->entries[i].key,
                                    &src->entries[i].value);
        if (s != PG_OK) return s;
    }
    return PG_OK;
}

/*
 * Merge properties from src into dst. Existing keys in dst are
 * not overwritten.
 */
pg_status pg_props_merge(pg_prop_store *dst, const pg_prop_store *src) {
    for (uint32_t i = 0; i < src->count; i++) {
        if (!pg_props_has(dst, src->entries[i].key)) {
            pg_status s = pg_props_set(dst, src->entries[i].key,
                                        &src->entries[i].value);
            if (s != PG_OK) return s;
        }
    }
    return PG_OK;
}

/*
 * Compare a property value against an expected value.
 */
bool pg_value_equals(const pg_value *a, const pg_value *b) {
    if (a->type != b->type) return false;
    switch (a->type) {
    case PG_VAL_INT:    return a->v.i == b->v.i;
    case PG_VAL_BOOL:   return a->v.b == b->v.b;
    case PG_VAL_FLOAT:  return a->v.f == b->v.f;
    case PG_VAL_STRING:
        if (a->v.str.len != b->v.str.len) return false;
        return memcmp(a->v.str.data, b->v.str.data, a->v.str.len) == 0;
    case PG_VAL_BLOB:
        if (a->v.blob.len != b->v.blob.len) return false;
        return memcmp(a->v.blob.data, b->v.blob.data, a->v.blob.len) == 0;
    default:
        return true;
    }
}

/*
 * Get the size in bytes of a property value's heap allocation.
 */
uint32_t pg_value_heap_size(const pg_value *v) {
    switch (v->type) {
    case PG_VAL_STRING: return v->v.str.len + 1;
    case PG_VAL_BLOB:   return v->v.blob.len;
    default:            return 0;
    }
}

/*
 * Get total heap memory used by all properties in a store.
 */
uint32_t pg_props_heap_size(const pg_prop_store *ps) {
    uint32_t total = ps->capacity * sizeof(pg_prop_entry);
    for (uint32_t i = 0; i < ps->count; i++) {
        total += pg_value_heap_size(&ps->entries[i].value);
    }
    return total;
}
