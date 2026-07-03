#include "cursor.h"
#include "parsegraph/graph.h"
#include "../util/error.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_cursor_init(pg_cursor *cur, pg_graph *g) {
    cur->graph = g;
    cur->current_node = NULL;
    cur->current_id = PG_INVALID_NODE;
    cur->adj_cache = NULL;
    cur->adj_pos = 0;
    cur->adj_count = 0;
    cur->exhausted = false;
    return PG_OK;
}

void pg_cursor_close(pg_cursor *cur) {
    cur->current_node = NULL;
    cur->adj_cache = NULL;
    cur->exhausted = true;
}

/*
 * BUG-1 ALIAS SITE:
 * Caches a raw pointer to the pg_node in the arena segment.
 * If compaction later relocates this segment (compact_nodes),
 * the cached pointer becomes dangling.
 *
 * The missing abstraction: cursor should acquire a lease on
 * the segment via a cursor-lease registry.
 */
pg_status pg_cursor_seek_node(pg_cursor *cur, pg_node_id id) {
    pg_node *n = pg_graph_get_node(cur->graph, id);
    if (!n) return PG_ERR_NOT_FOUND;
    cur->current_node = n;               /* ← alias: caches raw pointer */
    cur->current_id = id;
    cur->adj_cache = NULL;
    cur->adj_pos = 0;
    cur->adj_count = 0;
    cur->exhausted = false;
    return PG_OK;
}

/*
 * BUG-4 ALIAS SITE:
 * Caches a raw pointer into the node's adjacency list edge array.
 * If edges are added to this node later (adjacency_append reallocs),
 * the cached pointer is freed by realloc.
 */
pg_status pg_cursor_follow_edge(pg_cursor *cur, bool outgoing) {
    if (!cur->current_node) return PG_ERR_NOT_FOUND;
    pg_adj_list *adj = outgoing ? &cur->current_node->outgoing
                                : &cur->current_node->incoming;
    cur->adj_cache = adj->edges;         /* ← alias: caches raw pointer */
    cur->adj_pos = 0;
    cur->adj_count = adj->count;
    return PG_OK;
}

pg_node *pg_cursor_current(pg_cursor *cur) {
    return cur->current_node;
}

/*
 * BUG-4 CRASH SITE:
 * Reads through the cached adj_cache pointer.  If adjacency_append
 * has reallocated the underlying array, this reads freed memory.
 */
bool pg_cursor_next_neighbor(pg_cursor *cur, pg_edge_id *out_eid) {
    if (!cur->adj_cache || cur->adj_pos >= cur->adj_count)
        return false;
    *out_eid = cur->adj_cache[cur->adj_pos];     /* ← crash site */
    cur->adj_pos++;
    return true;
}

pg_status pg_cursor_reset(pg_cursor *cur) {
    cur->current_node = NULL;
    cur->current_id = PG_INVALID_NODE;
    cur->adj_cache = NULL;
    cur->adj_pos = 0;
    cur->adj_count = 0;
    cur->exhausted = false;
    return PG_OK;
}

/* ── Multi-hop traversal ─────────────────────────────────────── */

typedef struct pg_cursor_path {
    pg_node_id *nodes;
    pg_edge_id *edges;
    uint32_t    depth;
    uint32_t    capacity;
} pg_cursor_path;

pg_status pg_cursor_path_init(pg_cursor_path *path) {
    path->capacity = 16;
    path->nodes = malloc(path->capacity * sizeof(pg_node_id));
    if (!path->nodes) return PG_ERR_NOMEM;
    path->edges = malloc(path->capacity * sizeof(pg_edge_id));
    if (!path->edges) { free(path->nodes); return PG_ERR_NOMEM; }
    path->depth = 0;
    return PG_OK;
}

void pg_cursor_path_free(pg_cursor_path *path) {
    free(path->nodes);
    free(path->edges);
    path->nodes = NULL;
    path->edges = NULL;
    path->depth = 0;
}

pg_status pg_cursor_path_push(pg_cursor_path *path, pg_node_id nid,
                               pg_edge_id eid) {
    if (path->depth >= path->capacity) {
        uint32_t nc = path->capacity * 2;
        pg_node_id *nn = realloc(path->nodes, nc * sizeof(pg_node_id));
        if (!nn) return PG_ERR_NOMEM;
        path->nodes = nn;
        pg_edge_id *ne = realloc(path->edges, nc * sizeof(pg_edge_id));
        if (!ne) return PG_ERR_NOMEM;
        path->edges = ne;
        path->capacity = nc;
    }
    path->nodes[path->depth] = nid;
    path->edges[path->depth] = eid;
    path->depth++;
    return PG_OK;
}

bool pg_cursor_path_pop(pg_cursor_path *path, pg_node_id *nid,
                         pg_edge_id *eid) {
    if (path->depth == 0) return false;
    path->depth--;
    *nid = path->nodes[path->depth];
    *eid = path->edges[path->depth];
    return true;
}

bool pg_cursor_path_contains(pg_cursor_path *path, pg_node_id nid) {
    for (uint32_t i = 0; i < path->depth; i++) {
        if (path->nodes[i] == nid) return true;
    }
    return false;
}

/*
 * Depth-limited traversal: follows edges from current node up to
 * max_depth hops, calling the visitor function for each reached node.
 * Uses the cursor's cached adjacency pointer (Bug-4 relevant).
 */
typedef bool (*pg_cursor_visit_fn)(pg_node *node, pg_edge *edge,
                                    uint32_t depth, void *ctx);

pg_status pg_cursor_traverse_dfs(pg_cursor *cur, uint32_t max_depth,
                                  pg_cursor_visit_fn visitor, void *ctx) {
    if (!cur->current_node) return PG_ERR_NOT_FOUND;
    if (max_depth == 0) return PG_OK;

    pg_cursor_path path;
    pg_cursor_path_init(&path);

    /* start DFS from current node */
    pg_node *start = cur->current_node;
    pg_cursor_path_push(&path, start->id, PG_INVALID_EDGE);

    /* iterative DFS using adjacency cache */
    pg_cursor_follow_edge(cur, true);
    pg_edge_id eid;
    while (pg_cursor_next_neighbor(cur, &eid)) {
        pg_edge *edge = pg_graph_get_edge(cur->graph, eid);
        if (!edge) continue;

        pg_node_id dst_id = edge->dst;
        if (pg_cursor_path_contains(&path, dst_id)) continue;

        pg_node *dst = pg_graph_get_node(cur->graph, dst_id);
        if (!dst) continue;

        if (!visitor(dst, edge, path.depth, ctx)) break;

        if (path.depth < max_depth) {
            pg_cursor_path_push(&path, dst_id, eid);

            /* recurse: seek to destination and follow its edges */
            pg_cursor old = *cur;
            pg_cursor_seek_node(cur, dst_id);
            pg_cursor_follow_edge(cur, true);

            pg_edge_id sub_eid;
            while (pg_cursor_next_neighbor(cur, &sub_eid)) {
                pg_edge *sub_edge = pg_graph_get_edge(cur->graph, sub_eid);
                if (!sub_edge) continue;
                pg_node *sub_dst = pg_graph_get_node(cur->graph, sub_edge->dst);
                if (!sub_dst) continue;
                if (pg_cursor_path_contains(&path, sub_dst->id)) continue;
                visitor(sub_dst, sub_edge, path.depth, ctx);
            }

            pg_node_id pop_nid; pg_edge_id pop_eid;
            pg_cursor_path_pop(&path, &pop_nid, &pop_eid);
            *cur = old;
        }
    }

    pg_cursor_path_free(&path);
    return PG_OK;
}

/* BFS traversal variant */
pg_status pg_cursor_traverse_bfs(pg_cursor *cur, uint32_t max_depth,
                                  pg_cursor_visit_fn visitor, void *ctx) {
    if (!cur->current_node) return PG_ERR_NOT_FOUND;

    /* use a simple queue: array of (node_id, depth) pairs */
    typedef struct { pg_node_id id; uint32_t depth; } bfs_entry;
    uint32_t q_cap = 256;
    bfs_entry *queue = malloc(q_cap * sizeof(bfs_entry));
    if (!queue) return PG_ERR_NOMEM;

    uint32_t *visited = calloc(q_cap, sizeof(uint32_t));
    if (!visited) { free(queue); return PG_ERR_NOMEM; }

    uint32_t q_head = 0, q_tail = 0, v_count = 0;

    queue[q_tail++] = (bfs_entry){ cur->current_id, 0 };
    visited[v_count++] = (uint32_t)cur->current_id;

    while (q_head < q_tail) {
        bfs_entry entry = queue[q_head++];
        if (entry.depth >= max_depth) continue;

        pg_cursor_seek_node(cur, entry.id);
        pg_cursor_follow_edge(cur, true);

        pg_edge_id eid;
        while (pg_cursor_next_neighbor(cur, &eid)) {
            pg_edge *edge = pg_graph_get_edge(cur->graph, eid);
            if (!edge) continue;

            /* check if visited */
            bool seen = false;
            for (uint32_t i = 0; i < v_count; i++) {
                if (visited[i] == (uint32_t)edge->dst) { seen = true; break; }
            }
            if (seen) continue;

            pg_node *dst = pg_graph_get_node(cur->graph, edge->dst);
            if (!dst) continue;

            if (!visitor(dst, edge, entry.depth + 1, ctx)) {
                free(queue); free(visited);
                return PG_OK;
            }

            if (v_count < q_cap) visited[v_count++] = (uint32_t)edge->dst;
            if (q_tail < q_cap) {
                queue[q_tail++] = (bfs_entry){ edge->dst, entry.depth + 1 };
            }
        }
    }

    free(queue);
    free(visited);
    return PG_OK;
}

/* Shortest path via BFS */
pg_status pg_cursor_shortest_path(pg_cursor *cur, pg_node_id target,
                                   pg_cursor_path *out_path) {
    if (!cur->current_node) return PG_ERR_NOT_FOUND;

    typedef struct { pg_node_id id; pg_edge_id via_edge; int32_t parent; } sp_entry;
    uint32_t cap = 512;
    sp_entry *entries = malloc(cap * sizeof(sp_entry));
    if (!entries) return PG_ERR_NOMEM;

    uint32_t head = 0, tail = 0;
    entries[tail++] = (sp_entry){ cur->current_id, PG_INVALID_EDGE, -1 };

    bool found = false;
    uint32_t found_idx = 0;

    while (head < tail && !found) {
        sp_entry ent = entries[head];
        pg_cursor_seek_node(cur, ent.id);
        pg_cursor_follow_edge(cur, true);

        pg_edge_id eid;
        while (pg_cursor_next_neighbor(cur, &eid)) {
            pg_edge *edge = pg_graph_get_edge(cur->graph, eid);
            if (!edge) continue;

            /* check if already in entries */
            bool seen = false;
            for (uint32_t i = 0; i < tail; i++) {
                if (entries[i].id == edge->dst) { seen = true; break; }
            }
            if (seen) continue;

            if (tail >= cap) break;
            entries[tail] = (sp_entry){ edge->dst, eid, (int32_t)head };
            tail++;

            if (edge->dst == target) {
                found = true;
                found_idx = tail - 1;
                break;
            }
        }
        head++;
    }

    if (found) {
        pg_cursor_path_init(out_path);
        /* reconstruct path backwards */
        uint32_t idx = found_idx;
        while (idx > 0 && entries[idx].parent >= 0) {
            pg_cursor_path_push(out_path, entries[idx].id, entries[idx].via_edge);
            idx = (uint32_t)entries[idx].parent;
        }
    }

    free(entries);
    return found ? PG_OK : PG_ERR_NOT_FOUND;
}
