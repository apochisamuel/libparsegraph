#include "graph_internal.h"
#include "parsegraph/graph.h"
#include "../util/error.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_graph_create(pg_graph **out) {
    pg_graph *g = calloc(1, sizeof(pg_graph));
    if (!g) return PG_ERR_NOMEM;

    pg_status s;
    s = pg_arena_init(&g->node_arena, sizeof(pg_node), PG_SEGMENT_NODE_CAP);
    if (s != PG_OK) { free(g); return s; }

    s = pg_arena_init(&g->edge_arena, sizeof(pg_edge), PG_SEGMENT_EDGE_CAP);
    if (s != PG_OK) {
        pg_arena_destroy(&g->node_arena);
        free(g);
        return s;
    }

    s = pg_map_init(&g->node_map, 256);
    if (s != PG_OK) {
        pg_arena_destroy(&g->edge_arena);
        pg_arena_destroy(&g->node_arena);
        free(g);
        return s;
    }

    s = pg_map_init(&g->edge_map, 256);
    if (s != PG_OK) {
        pg_map_free(&g->node_map);
        pg_arena_destroy(&g->edge_arena);
        pg_arena_destroy(&g->node_arena);
        free(g);
        return s;
    }

    s = pg_labels_init(&g->labels);
    if (s != PG_OK) {
        pg_map_free(&g->edge_map);
        pg_map_free(&g->node_map);
        pg_arena_destroy(&g->edge_arena);
        pg_arena_destroy(&g->node_arena);
        free(g);
        return s;
    }

    pg_gen_init(&g->generation);
    g->next_node_id = 1;
    g->next_edge_id = 1;
    g->node_count = 0;
    g->edge_count = 0;
    g->mutation_count = 0;

    *out = g;
    return PG_OK;
}

static void free_node_contents(pg_node *n) {
    pg_adj_free(&n->outgoing);
    pg_adj_free(&n->incoming);
    pg_props_free(&n->properties);
}

static void free_edge_contents(pg_edge *e) {
    pg_props_free(&e->properties);
}

void pg_graph_destroy(pg_graph *g) {
    if (!g) return;

    /* free node contents */
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (seg->occupied[i]) {
                pg_node *n = (pg_node *)(seg->data +
                             (size_t)i * seg->slot_size);
                free_node_contents(n);
            }
        }
        seg = seg->next;
    }

    /* free edge contents */
    seg = g->edge_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (seg->occupied[i]) {
                pg_edge *e = (pg_edge *)(seg->data +
                             (size_t)i * seg->slot_size);
                free_edge_contents(e);
            }
        }
        seg = seg->next;
    }

    pg_arena_destroy(&g->node_arena);
    pg_arena_destroy(&g->edge_arena);
    pg_map_free(&g->node_map);
    pg_map_free(&g->edge_map);
    pg_labels_free(&g->labels);
    free(g);
}

pg_node_id pg_graph_add_node(pg_graph *g, pg_label_id label) {
    pg_node *n = pg_arena_alloc(&g->node_arena);
    if (!n) return PG_INVALID_NODE;

    pg_node_id id = g->next_node_id++;
    n->id = id;
    n->label = label;
    pg_adj_init(&n->outgoing);
    pg_adj_init(&n->incoming);
    pg_props_init(&n->properties);
    n->created_gen = pg_gen_current(&g->generation);
    n->deleted_gen = 0;
    n->refcount = 1;
    n->flags = 0;

    pg_map_put(&g->node_map, (uint64_t)id, n);
    g->node_count++;
    g->mutation_count++;
    return id;
}

/*
 * BUG-2 FREE SITE:
 * Removing a node frees the node's arena slot immediately.  If the
 * memo cache has a MatchResult containing a pointer to this node
 * (cached during a prior query in the same batch), the pointer
 * becomes stale.  A subsequent project_result_field() dereferences
 * the stale cached pointer → heap-use-after-free.
 *
 * The missing abstraction is generation-gated memo invalidation:
 * the cache should tag entries with the graph generation and reject
 * lookups when the generation has advanced past the entry's tag.
 */
pg_status pg_graph_remove_node(pg_graph *g, pg_node_id id) {
    pg_node *n = pg_map_remove(&g->node_map, (uint64_t)id);
    if (!n) return PG_ERR_NOT_FOUND;

    /* remove incident edges */
    for (uint32_t i = 0; i < n->outgoing.count; i++) {
        pg_edge_id eid = n->outgoing.edges[i];
        pg_edge *e = pg_map_remove(&g->edge_map, (uint64_t)eid);
        if (e) {
            /* remove from dst's incoming list */
            pg_node *dst = pg_map_get(&g->node_map, (uint64_t)e->dst);
            if (dst) pg_adj_remove(&dst->incoming, eid);
            free_edge_contents(e);
            pg_arena_free_slot(&g->edge_arena, e);
            g->edge_count--;
        }
    }
    for (uint32_t i = 0; i < n->incoming.count; i++) {
        pg_edge_id eid = n->incoming.edges[i];
        pg_edge *e = pg_map_remove(&g->edge_map, (uint64_t)eid);
        if (e) {
            pg_node *src = pg_map_get(&g->node_map, (uint64_t)e->src);
            if (src) pg_adj_remove(&src->outgoing, eid);
            free_edge_contents(e);
            pg_arena_free_slot(&g->edge_arena, e);
            g->edge_count--;
        }
    }

    free_node_contents(n);
    pg_arena_free_slot(&g->node_arena, n);          /* ← free site */
    g->node_count--;
    g->mutation_count++;
    pg_gen_advance(&g->generation);
    return PG_OK;
}

pg_node *pg_graph_get_node(pg_graph *g, pg_node_id id) {
    return pg_map_get(&g->node_map, (uint64_t)id);
}

uint32_t pg_graph_node_count(const pg_graph *g) {
    return g->node_count;
}

pg_edge_id pg_graph_add_edge(pg_graph *g, pg_node_id src_id,
                              pg_node_id dst_id, pg_label_id edge_type) {
    pg_node *src = pg_graph_get_node(g, src_id);
    pg_node *dst = pg_graph_get_node(g, dst_id);
    if (!src || !dst) return PG_INVALID_EDGE;

    pg_edge *e = pg_arena_alloc(&g->edge_arena);
    if (!e) return PG_INVALID_EDGE;

    pg_edge_id id = g->next_edge_id++;
    e->id = id;
    e->src = src_id;
    e->dst = dst_id;
    e->edge_type = edge_type;
    pg_props_init(&e->properties);
    e->created_gen = pg_gen_current(&g->generation);
    e->deleted_gen = 0;
    e->refcount = 1;
    e->flags = 0;

    pg_map_put(&g->edge_map, (uint64_t)id, e);

    /* BUG-4 TRIGGER: adjacency append can realloc while cursors
       hold cached pointers into the old array */
    pg_adj_append(&src->outgoing, id);
    pg_adj_append(&dst->incoming, id);

    g->edge_count++;
    g->mutation_count++;
    return id;
}

pg_status pg_graph_remove_edge(pg_graph *g, pg_edge_id id) {
    pg_edge *e = pg_map_remove(&g->edge_map, (uint64_t)id);
    if (!e) return PG_ERR_NOT_FOUND;

    pg_node *src = pg_graph_get_node(g, e->src);
    pg_node *dst = pg_graph_get_node(g, e->dst);
    if (src) pg_adj_remove(&src->outgoing, id);
    if (dst) pg_adj_remove(&dst->incoming, id);

    free_edge_contents(e);
    pg_arena_free_slot(&g->edge_arena, e);
    g->edge_count--;
    g->mutation_count++;
    return PG_OK;
}

pg_edge *pg_graph_get_edge(pg_graph *g, pg_edge_id id) {
    return pg_map_get(&g->edge_map, (uint64_t)id);
}

uint32_t pg_graph_edge_count(const pg_graph *g) {
    return g->edge_count;
}

pg_status pg_graph_set_node_prop(pg_graph *g, pg_node_id id,
                                  pg_prop_key key, const pg_value *val) {
    pg_node *n = pg_graph_get_node(g, id);
    if (!n) return PG_ERR_NOT_FOUND;
    g->mutation_count++;
    return pg_props_set(&n->properties, key, val);
}

pg_value *pg_graph_get_node_prop(pg_graph *g, pg_node_id id,
                                  pg_prop_key key) {
    pg_node *n = pg_graph_get_node(g, id);
    if (!n) return NULL;
    return pg_props_get(&n->properties, key);
}

pg_label_id pg_graph_intern_label(pg_graph *g, const char *name,
                                   uint32_t len) {
    return pg_labels_intern(&g->labels, name, len);
}

const char *pg_graph_resolve_label(pg_graph *g, pg_label_id id) {
    return pg_labels_resolve(&g->labels, id);
}

pg_generation pg_graph_generation(const pg_graph *g) {
    return pg_gen_current(&g->generation);
}

uint32_t pg_graph_mutation_count(const pg_graph *g) {
    return g->mutation_count;
}

pg_status pg_graph_snapshot(pg_graph *g, pg_snapshot **out) {
    pg_generation gen = pg_gen_current(&g->generation);
    pg_status s = pg_snapshot_create(out, gen);
    if (s != PG_OK) return s;

    /* capture references to all live nodes */
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (seg->occupied[i]) {
                pg_node *n = (pg_node *)(seg->data +
                             (size_t)i * seg->slot_size);
                if (n->deleted_gen == 0) {
                    pg_snapshot_add_ref(*out, n, n->id);
                }
            }
        }
        seg = seg->next;
    }
    return PG_OK;
}

struct pg_arena *pg_graph_node_arena(pg_graph *g) {
    return &g->node_arena;
}

struct pg_arena *pg_graph_edge_arena(pg_graph *g) {
    return &g->edge_arena;
}

/* ── Graph iteration and utilities ───────────────────────── */

typedef bool (*pg_node_visitor_fn)(pg_node *node, void *ctx);
typedef bool (*pg_edge_visitor_fn)(pg_edge *edge, void *ctx);

/*
 * Iterate all live nodes in the graph, calling the visitor for each.
 */
void pg_graph_foreach_node(pg_graph *g, pg_node_visitor_fn fn, void *ctx) {
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_node *n = (pg_node *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (n->deleted_gen == 0) {
                if (!fn(n, ctx)) return;
            }
        }
        seg = seg->next;
    }
}

/*
 * Iterate all live edges in the graph.
 */
void pg_graph_foreach_edge(pg_graph *g, pg_edge_visitor_fn fn, void *ctx) {
    pg_arena_segment *seg = g->edge_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_edge *e = (pg_edge *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (e->deleted_gen == 0) {
                if (!fn(e, ctx)) return;
            }
        }
        seg = seg->next;
    }
}

/*
 * Count nodes with a specific label.
 */
uint32_t pg_graph_count_by_label(pg_graph *g, pg_label_id label) {
    uint32_t count = 0;
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_node *n = (pg_node *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (n->deleted_gen == 0 && n->label == label)
                count++;
        }
        seg = seg->next;
    }
    return count;
}

/*
 * Find the node with the highest degree (most edges).
 */
pg_node *pg_graph_highest_degree(pg_graph *g) {
    pg_node *best = NULL;
    uint32_t best_degree = 0;
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_node *n = (pg_node *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (n->deleted_gen != 0) continue;
            uint32_t degree = n->outgoing.count + n->incoming.count;
            if (degree > best_degree) {
                best_degree = degree;
                best = n;
            }
        }
        seg = seg->next;
    }
    return best;
}

/*
 * Compute graph statistics.
 */
typedef struct pg_graph_stats {
    uint32_t total_nodes;
    uint32_t total_edges;
    uint32_t total_properties;
    uint32_t max_out_degree;
    uint32_t max_in_degree;
    float    avg_degree;
    float    arena_utilization;
} pg_graph_stats;

void pg_graph_compute_stats(pg_graph *g, pg_graph_stats *stats) {
    stats->total_nodes = g->node_count;
    stats->total_edges = g->edge_count;
    stats->total_properties = 0;
    stats->max_out_degree = 0;
    stats->max_in_degree = 0;
    stats->arena_utilization = pg_arena_utilization(&g->node_arena);

    uint32_t total_degree = 0;
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        for (uint32_t i = 0; i < seg->slot_count; i++) {
            if (!seg->occupied[i]) continue;
            pg_node *n = (pg_node *)(seg->data +
                         (size_t)i * seg->slot_size);
            if (n->deleted_gen != 0) continue;

            stats->total_properties += n->properties.count;
            if (n->outgoing.count > stats->max_out_degree)
                stats->max_out_degree = n->outgoing.count;
            if (n->incoming.count > stats->max_in_degree)
                stats->max_in_degree = n->incoming.count;
            total_degree += n->outgoing.count + n->incoming.count;
        }
        seg = seg->next;
    }

    if (g->node_count > 0) {
        stats->avg_degree = (float)total_degree / (float)g->node_count;
    } else {
        stats->avg_degree = 0.0f;
    }
}

/*
 * Set an edge property.
 */
pg_status pg_graph_set_edge_prop(pg_graph *g, pg_edge_id id,
                                  pg_prop_key key, const pg_value *val) {
    pg_edge *e = pg_graph_get_edge(g, id);
    if (!e) return PG_ERR_NOT_FOUND;
    g->mutation_count++;
    return pg_props_set(&e->properties, key, val);
}

/*
 * Get an edge property.
 */
pg_value *pg_graph_get_edge_prop(pg_graph *g, pg_edge_id id,
                                  pg_prop_key key) {
    pg_edge *e = pg_graph_get_edge(g, id);
    if (!e) return NULL;
    return pg_props_get(&e->properties, key);
}

/*
 * Get neighbors of a node (IDs of nodes connected via outgoing edges).
 */
pg_status pg_graph_get_neighbors(pg_graph *g, pg_node_id id,
                                  pg_node_id *out, uint32_t *count,
                                  uint32_t max) {
    pg_node *n = pg_graph_get_node(g, id);
    if (!n) return PG_ERR_NOT_FOUND;

    *count = 0;
    for (uint32_t i = 0; i < n->outgoing.count && *count < max; i++) {
        pg_edge *e = pg_graph_get_edge(g, n->outgoing.edges[i]);
        if (e) {
            out[*count] = e->dst;
            (*count)++;
        }
    }
    return PG_OK;
}

/*
 * Check if two nodes are directly connected.
 */
bool pg_graph_are_connected(pg_graph *g, pg_node_id a, pg_node_id b) {
    pg_node *na = pg_graph_get_node(g, a);
    if (!na) return false;

    for (uint32_t i = 0; i < na->outgoing.count; i++) {
        pg_edge *e = pg_graph_get_edge(g, na->outgoing.edges[i]);
        if (e && e->dst == b) return true;
    }
    return false;
}

/*
 * Remove a node property.
 */
pg_status pg_graph_remove_node_prop(pg_graph *g, pg_node_id id,
                                     pg_prop_key key) {
    pg_node *n = pg_graph_get_node(g, id);
    if (!n) return PG_ERR_NOT_FOUND;
    g->mutation_count++;
    return pg_props_remove(&n->properties, key);
}
