#include "edge_index.h"
#include "../util/vector.h"
#include "../core/graph_internal.h"
#include <stdlib.h>

pg_status pg_edge_index_init(pg_edge_index *idx) {
    idx->total_entries = 0;
    return pg_hash_init(&idx->table, 32);
}

void pg_edge_index_free(pg_edge_index *idx) {
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

pg_status pg_edge_index_insert(pg_edge_index *idx, pg_label_id type,
                                pg_edge_id eid) {
    pg_vector *vec = pg_hash_get(&idx->table, (uint64_t)type);
    if (!vec) {
        vec = malloc(sizeof(pg_vector));
        if (!vec) return PG_ERR_NOMEM;
        pg_vector_init(vec, sizeof(pg_edge_id), 8);
        pg_hash_put(&idx->table, (uint64_t)type, vec);
    }
    pg_vector_push(vec, &eid);
    idx->total_entries++;
    return PG_OK;
}

pg_status pg_edge_index_remove(pg_edge_index *idx, pg_label_id type,
                                pg_edge_id eid) {
    pg_vector *vec = pg_hash_get(&idx->table, (uint64_t)type);
    if (!vec) return PG_ERR_NOT_FOUND;
    for (uint32_t i = 0; i < pg_vector_count(vec); i++) {
        pg_edge_id *id = pg_vector_get(vec, i);
        if (*id == eid) {
            pg_vector_remove(vec, i);
            idx->total_entries--;
            return PG_OK;
        }
    }
    return PG_ERR_NOT_FOUND;
}

/* ── Extended edge index operations ──────────────────────── */

/*
 * Count edges of a specific type.
 */
uint32_t pg_edge_index_count(pg_edge_index *idx, pg_label_id type) {
    pg_vector *vec = pg_hash_get(&idx->table, (uint64_t)type);
    if (!vec) return 0;
    return pg_vector_count(vec);
}

/*
 * Get all distinct edge types in the index.
 */
pg_status pg_edge_index_types(pg_edge_index *idx,
                               pg_label_id *out, uint32_t *count,
                               uint32_t max) {
    *count = 0;
    pg_hash_iter it;
    pg_hash_iter_init(&it, &idx->table);
    uint64_t key;
    void *value;
    while (pg_hash_iter_next(&it, &key, &value) && *count < max) {
        out[*count] = (pg_label_id)key;
        (*count)++;
    }
    return PG_OK;
}

/*
 * Rebuild edge index from the graph.
 */
pg_status pg_edge_index_rebuild(pg_edge_index *idx, pg_graph *g) {
    /* clear existing vectors */
    pg_hash_iter it;
    uint64_t k; void *v;
    pg_hash_iter_init(&it, &idx->table);
    while (pg_hash_iter_next(&it, &k, &v)) {
        pg_vector *vec = v;
        pg_vector_clear(vec);
    }
    idx->total_entries = 0;

    /* scan all edges and re-index */
    pg_arena_segment *seg = g->edge_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_edge *e = (pg_edge *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (e->deleted_gen != 0) continue;
            pg_edge_index_insert(idx, e->edge_type, e->id);
        }
        seg = seg->next;
    }
    return PG_OK;
}
