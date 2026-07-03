#ifndef PG_CURSOR_H
#define PG_CURSOR_H

#include "parsegraph/types.h"
#include "../core/graph_internal.h"

/*
 * Graph traversal cursor — holds a cached pointer to the current
 * node and optionally a cached pointer into the adjacency array
 * for edge iteration.
 *
 * BUG-1 ALIAS: cursor_seek_node caches a GraphNode* from a segment.
 * BUG-4 ALIAS: cursor_follow_edge caches a pointer into adj array.
 */

typedef struct pg_cursor {
    pg_graph    *graph;
    pg_node     *current_node;    /* cached pointer into segment */
    pg_node_id   current_id;
    pg_edge_id  *adj_cache;       /* cached pointer into adjacency array */
    uint32_t     adj_pos;
    uint32_t     adj_count;
    bool         exhausted;
} pg_cursor;

pg_status pg_cursor_init(pg_cursor *cur, pg_graph *g);
void      pg_cursor_close(pg_cursor *cur);
pg_status pg_cursor_seek_node(pg_cursor *cur, pg_node_id id);
pg_status pg_cursor_follow_edge(pg_cursor *cur, bool outgoing);
pg_node  *pg_cursor_current(pg_cursor *cur);
bool      pg_cursor_next_neighbor(pg_cursor *cur, pg_edge_id *out_eid);
pg_status pg_cursor_reset(pg_cursor *cur);

#endif
