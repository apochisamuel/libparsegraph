#include "label_index.h"
#include "../core/graph_internal.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_label_index_init(pg_label_index *idx) {
    pg_status s = pg_hash_init(&idx->table, 64);
    if (s != PG_OK) return s;
    idx->total_entries = 0;
    return PG_OK;
}

static void free_vectors(uint64_t key, void *value, void *ctx) {
    (void)key; (void)ctx;
    pg_vector *v = value;
    pg_vector_free(v);
    free(v);
}

void pg_label_index_free(pg_label_index *idx) {
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

/*
 * BUG-6 TRIGGER:
 * Inserting into the index can trigger pg_hash_grow() on the
 * underlying table, which frees the old bucket array.
 * If an active pg_label_scan holds a pointer into the old
 * vector (which itself was found through the old buckets),
 * subsequent scan_next calls read from freed memory.
 */
pg_status pg_label_index_insert(pg_label_index *idx, pg_label_id label,
                                 pg_node_id node) {
    pg_vector *vec = pg_hash_get(&idx->table, (uint64_t)label);
    if (!vec) {
        vec = malloc(sizeof(pg_vector));
        if (!vec) return PG_ERR_NOMEM;
        pg_status s = pg_vector_init(vec, sizeof(pg_node_id), 8);
        if (s != PG_OK) { free(vec); return s; }
        s = pg_hash_put(&idx->table, (uint64_t)label, vec);
        if (s != PG_OK) {
            pg_vector_free(vec);
            free(vec);
            return s;
        }
    }
    pg_status s = pg_vector_push(vec, &node);
    if (s != PG_OK) return s;
    idx->total_entries++;
    return PG_OK;
}

pg_status pg_label_index_remove(pg_label_index *idx, pg_label_id label,
                                 pg_node_id node) {
    pg_vector *vec = pg_hash_get(&idx->table, (uint64_t)label);
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

pg_status pg_label_scan_begin(pg_label_index *idx, pg_label_id label,
                               pg_label_scan *scan) {
    pg_vector *vec = pg_hash_get(&idx->table, (uint64_t)label);
    scan->nodes = vec;         /* may be NULL if no nodes with this label */
    scan->pos = 0;
    scan->label = label;
    return PG_OK;
}

bool pg_label_scan_next(pg_label_scan *scan, pg_node_id *out) {
    if (!scan->nodes || scan->pos >= pg_vector_count(scan->nodes))
        return false;
    pg_node_id *id = pg_vector_get(scan->nodes, scan->pos);
    *out = *id;
    scan->pos++;
    return true;
}

void pg_label_scan_end(pg_label_scan *scan) {
    scan->nodes = NULL;
    scan->pos = 0;
}

/* ── Extended label index operations ─────────────────────── */

/*
 * Count nodes with a given label in the index.
 */
uint32_t pg_label_index_count(pg_label_index *idx, pg_label_id label) {
    pg_vector *vec = pg_hash_get(&idx->table, (uint64_t)label);
    if (!vec) return 0;
    return pg_vector_count(vec);
}

/*
 * Get all distinct labels in the index.
 */
pg_status pg_label_index_labels(pg_label_index *idx,
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
 * Rebuild the label index from scratch by scanning all nodes.
 * Clears existing index data first.
 */
pg_status pg_label_index_rebuild(pg_label_index *idx, pg_graph *g) {
    /* clear existing */
    pg_hash_iter it;
    uint64_t k; void *v;
    pg_hash_iter_init(&it, &idx->table);
    while (pg_hash_iter_next(&it, &k, &v)) {
        pg_vector *vec = v;
        pg_vector_clear(vec);
    }
    idx->total_entries = 0;

    /* scan all nodes and re-index */
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_node *n = (pg_node *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (n->deleted_gen != 0) continue;
            pg_label_index_insert(idx, n->label, n->id);
        }
        seg = seg->next;
    }
    return PG_OK;
}

/*
 * Scan with a callback instead of iterator pattern.
 */
typedef bool (*pg_label_scan_fn)(pg_node_id id, void *ctx);

void pg_label_index_scan_each(pg_label_index *idx, pg_label_id label,
                               pg_label_scan_fn fn, void *ctx) {
    pg_vector *vec = pg_hash_get(&idx->table, (uint64_t)label);
    if (!vec) return;

    for (uint32_t i = 0; i < pg_vector_count(vec); i++) {
        pg_node_id *id = pg_vector_get(vec, i);
        if (!fn(*id, ctx)) return;
    }
}
