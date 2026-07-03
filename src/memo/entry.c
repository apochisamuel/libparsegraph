#include "entry.h"
#include <stdlib.h>
#include <string.h>

pg_cache_entry *pg_entry_create(uint64_t key, pg_match_result *result,
                                 pg_generation gen) {
    pg_cache_entry *e = calloc(1, sizeof(pg_cache_entry));
    if (!e) return NULL;
    e->key = key;
    e->stored_gen = gen;
    e->access_count = 0;
    e->lru_stamp = 0;
    e->valid = true;

    /* shallow copy: node/edge pointers are shared, not cloned.
     * BUG-2: these pointers can become stale if the underlying
     * nodes are freed via graph_remove_node. */
    pg_match_result_init(&e->result);
    for (uint32_t i = 0; i < result->node_count; i++) {
        pg_match_result_add_node(&e->result, result->nodes[i]);
    }
    for (uint32_t i = 0; i < result->edge_count; i++) {
        pg_match_result_add_edge(&e->result, result->edges[i]);
    }
    return e;
}

void pg_entry_destroy(pg_cache_entry *entry) {
    if (!entry) return;
    pg_match_result_free(&entry->result);
    entry->valid = false;
    free(entry);
}

/*
 * BUG-8 ALIAS SITE:
 * Returns the entry pointer after marking it as accessed.
 * The caller (executor) holds this pointer while potentially
 * triggering a new store → eviction → this entry freed.
 */
pg_cache_entry *pg_entry_access(pg_cache_entry *entry) {
    if (!entry || !entry->valid) return NULL;
    entry->access_count++;
    return entry;                       /* ← alias: raw pointer returned */
}

/* ── Extended entry operations ───────────────────────────── */

/*
 * Check if an entry's result references a specific node ID.
 */
bool pg_entry_references_node(pg_cache_entry *entry, pg_node_id id) {
    if (!entry || !entry->valid) return false;
    for (uint32_t i = 0; i < entry->result.node_count; i++) {
        if (entry->result.nodes[i] && entry->result.nodes[i]->id == id)
            return true;
    }
    return false;
}

/*
 * Deep clone an entry. The cloned entry has its own result.
 */
pg_cache_entry *pg_entry_clone(pg_cache_entry *entry) {
    if (!entry) return NULL;
    pg_cache_entry *c = calloc(1, sizeof(pg_cache_entry));
    if (!c) return NULL;
    c->key = entry->key;
    c->stored_gen = entry->stored_gen;
    c->access_count = 0;
    c->lru_stamp = entry->lru_stamp;
    c->valid = entry->valid;

    pg_match_result_init(&c->result);
    for (uint32_t i = 0; i < entry->result.node_count; i++) {
        pg_match_result_add_node(&c->result, entry->result.nodes[i]);
    }
    for (uint32_t i = 0; i < entry->result.edge_count; i++) {
        pg_match_result_add_edge(&c->result, entry->result.edges[i]);
    }
    return c;
}

/*
 * Get the size in bytes of an entry's result.
 */
uint32_t pg_entry_result_size(pg_cache_entry *entry) {
    if (!entry) return 0;
    uint32_t sz = sizeof(pg_cache_entry);
    sz += entry->result.node_count * sizeof(pg_node *);
    sz += entry->result.edge_count * sizeof(pg_edge *);
    return sz;
}
