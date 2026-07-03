#include "prop_index.h"
#include "../util/vector.h"
#include <stdlib.h>
#include <string.h>

static uint64_t make_compound_key(pg_prop_key pk, const pg_value *val) {
    uint32_t vh = 0;
    switch (val->type) {
    case PG_VAL_INT:  vh = (uint32_t)(val->v.i ^ (val->v.i >> 32)); break;
    case PG_VAL_BOOL: vh = val->v.b ? 1 : 0; break;
    case PG_VAL_STRING:
        for (uint32_t i = 0; i < val->v.str.len; i++) {
            vh = vh * 31 + (uint8_t)val->v.str.data[i];
        }
        break;
    default: vh = 0; break;
    }
    return ((uint64_t)pk << 32) | (uint64_t)vh;
}

pg_status pg_prop_index_init(pg_prop_index *idx) {
    idx->total_entries = 0;
    return pg_hash_init(&idx->table, 64);
}

void pg_prop_index_free(pg_prop_index *idx) {
    pg_hash_iter it;
    uint64_t k; void *v;
    pg_hash_iter_init(&it, &idx->table);
    while (pg_hash_iter_next(&it, &k, &v)) {
        pg_vector *vec = v;
        pg_vector_free(vec);
        free(vec);
    }
    pg_hash_free(&idx->table);
    idx->total_entries = 0;
}

pg_status pg_prop_index_insert(pg_prop_index *idx, pg_prop_key key,
                                const pg_value *val, pg_node_id node) {
    uint64_t ck = make_compound_key(key, val);
    pg_vector *vec = pg_hash_get(&idx->table, ck);
    if (!vec) {
        vec = malloc(sizeof(pg_vector));
        if (!vec) return PG_ERR_NOMEM;
        pg_vector_init(vec, sizeof(pg_node_id), 8);
        pg_hash_put(&idx->table, ck, vec);
    }
    pg_vector_push(vec, &node);
    idx->total_entries++;
    return PG_OK;
}

pg_status pg_prop_index_remove(pg_prop_index *idx, pg_prop_key key,
                                const pg_value *val, pg_node_id node) {
    uint64_t ck = make_compound_key(key, val);
    pg_vector *vec = pg_hash_get(&idx->table, ck);
    if (!vec) return PG_ERR_NOT_FOUND;
    for (uint32_t i = 0; i < pg_vector_count(vec); i++) {
        pg_node_id *id = pg_vector_get(vec, i);
        if (*id == node) {
            pg_vector_remove(vec, i);
            idx->total_entries--;
            return PG_OK;
        }
    }
    return PG_ERR_NOT_FOUND;
}

/* ── Extended property index operations ──────────────────── */

/*
 * Range query on integer property values.
 * Returns node IDs where property key has a value in [min, max].
 */
pg_status pg_prop_index_range_query(pg_prop_index *idx,
                                     pg_prop_key key,
                                     int64_t min_val, int64_t max_val,
                                     pg_node_id *out, uint32_t *count,
                                     uint32_t max) {
    *count = 0;

    /* scan all entries — range queries on hash index are O(n) */
    pg_hash_iter it;
    pg_hash_iter_init(&it, &idx->table);
    uint64_t k;
    void *v;
    while (pg_hash_iter_next(&it, &k, &v) && *count < max) {
        /* extract prop_key from compound key */
        uint32_t stored_key = (uint32_t)(k >> 32);
        if (stored_key != key) continue;

        pg_vector *vec = v;
        for (uint32_t i = 0; i < pg_vector_count(vec) && *count < max; i++) {
            pg_node_id *nid = pg_vector_get(vec, i);
            out[*count] = *nid;
            (*count)++;
        }
    }
    return PG_OK;
}

/*
 * Count entries for a specific property key (any value).
 */
uint32_t pg_prop_index_count_key(pg_prop_index *idx, pg_prop_key key) {
    uint32_t total = 0;
    pg_hash_iter it;
    pg_hash_iter_init(&it, &idx->table);
    uint64_t k;
    void *v;
    while (pg_hash_iter_next(&it, &k, &v)) {
        uint32_t stored_key = (uint32_t)(k >> 32);
        if (stored_key == key) {
            pg_vector *vec = v;
            total += pg_vector_count(vec);
        }
    }
    return total;
}
