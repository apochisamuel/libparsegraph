#ifndef PG_COMPACT_H
#define PG_COMPACT_H

#include "parsegraph/types.h"
#include "../core/arena.h"
#include "../core/graph_internal.h"
#include "segment.h"

/*
 * Compaction engine: relocates live objects from fragmented segments
 * into fresh segments, then releases the old segment.
 *
 * BUG-1 SITE: compact_execute releases old segments without checking
 * whether cursors still hold cached pointers into them.
 */

typedef struct pg_compact_ctx {
    pg_graph        *graph;
    pg_segment_mgr  *node_seg_mgr;
    pg_segment_mgr  *edge_seg_mgr;
    uint32_t         nodes_relocated;
    uint32_t         edges_relocated;
    uint32_t         segments_freed;
} pg_compact_ctx;

pg_status pg_compact_init(pg_compact_ctx *ctx, pg_graph *g,
                           pg_segment_mgr *nsm, pg_segment_mgr *esm);
pg_status pg_compact_execute(pg_compact_ctx *ctx);
bool      pg_compact_should_run(pg_graph *g);
pg_status pg_compact_nodes(pg_compact_ctx *ctx, pg_arena_segment *old_seg);
pg_status pg_compact_edges(pg_compact_ctx *ctx, pg_arena_segment *old_seg);

#endif /* PG_COMPACT_H */
