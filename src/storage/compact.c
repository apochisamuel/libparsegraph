#include "compact.h"
#include "parsegraph/graph.h"
#include "../util/error.h"
#include <string.h>

pg_status pg_compact_init(pg_compact_ctx *ctx, pg_graph *g,
                           pg_segment_mgr *nsm, pg_segment_mgr *esm) {
    ctx->graph = g;
    ctx->node_seg_mgr = nsm;
    ctx->edge_seg_mgr = esm;
    ctx->nodes_relocated = 0;
    ctx->edges_relocated = 0;
    ctx->segments_freed = 0;
    return PG_OK;
}

/*
 * Relocate live nodes from old_seg into a fresh segment.
 * Updates the node_map so lookups find the new address.
 */
pg_status pg_compact_nodes(pg_compact_ctx *ctx, pg_arena_segment *old_seg) {
    pg_arena *arena = pg_graph_node_arena(ctx->graph);
    pg_arena_segment *new_seg = pg_arena_new_segment(arena);
    if (!new_seg) return PG_ERR_NOMEM;

    for (uint32_t i = 0; i < old_seg->slot_count; i++) {
        if (!old_seg->occupied[i]) continue;

        pg_node *old_node = (pg_node *)(old_seg->data +
                             (size_t)i * old_seg->slot_size);

        /* allocate slot in new segment */
        if (new_seg->used >= new_seg->slot_count) {
            /* shouldn't happen if segments are same capacity */
            return PG_ERR_FULL;
        }
        uint32_t new_idx = new_seg->used;
        pg_node *new_node = (pg_node *)(new_seg->data +
                             (size_t)new_idx * new_seg->slot_size);
        memcpy(new_node, old_node, old_seg->slot_size);
        new_seg->occupied[new_idx] = 1;
        new_seg->used++;
        new_seg->live++;

        /* update the map to point to the relocated node */
        pg_map_put(&ctx->graph->node_map, (uint64_t)new_node->id, new_node);
        ctx->nodes_relocated++;
    }

    /*
     * BUG-1 FREE SITE:
     * Release the old segment.  This frees old_seg->data, which
     * contains the original pg_node structs.  If any cursor has
     * cached a pointer to one of these nodes via cursor_seek_node(),
     * the cursor's current_node pointer is now dangling.
     *
     * The missing abstraction is a cursor-lease registry: cursors
     * should acquire a lease on segments they reference, and
     * compact must drain all leases before releasing.
     */
    pg_arena_release_segment(arena, old_seg);       /* ← free site */
    ctx->segments_freed++;
    return PG_OK;
}

pg_status pg_compact_edges(pg_compact_ctx *ctx, pg_arena_segment *old_seg) {
    pg_arena *arena = pg_graph_edge_arena(ctx->graph);
    pg_arena_segment *new_seg = pg_arena_new_segment(arena);
    if (!new_seg) return PG_ERR_NOMEM;

    for (uint32_t i = 0; i < old_seg->slot_count; i++) {
        if (!old_seg->occupied[i]) continue;

        pg_edge *old_edge = (pg_edge *)(old_seg->data +
                             (size_t)i * old_seg->slot_size);

        if (new_seg->used >= new_seg->slot_count) return PG_ERR_FULL;
        uint32_t new_idx = new_seg->used;
        pg_edge *new_edge = (pg_edge *)(new_seg->data +
                             (size_t)new_idx * new_seg->slot_size);
        memcpy(new_edge, old_edge, old_seg->slot_size);
        new_seg->occupied[new_idx] = 1;
        new_seg->used++;
        new_seg->live++;

        pg_map_put(&ctx->graph->edge_map, (uint64_t)new_edge->id, new_edge);
        ctx->edges_relocated++;
    }

    pg_arena_release_segment(arena, old_seg);
    ctx->segments_freed++;
    return PG_OK;
}

pg_status pg_compact_execute(pg_compact_ctx *ctx) {
    /* compact worst node segment if fragmented enough */
    pg_segment_info *worst_n = pg_segment_mgr_worst(ctx->node_seg_mgr);
    if (worst_n && worst_n->frag_ratio > PG_COMPACT_THRESHOLD) {
        worst_n->compacting = true;
        pg_status s = pg_compact_nodes(ctx, worst_n->arena_seg);
        if (s != PG_OK) return s;
    }

    /* compact worst edge segment */
    pg_segment_info *worst_e = pg_segment_mgr_worst(ctx->edge_seg_mgr);
    if (worst_e && worst_e->frag_ratio > PG_COMPACT_THRESHOLD) {
        worst_e->compacting = true;
        pg_status s = pg_compact_edges(ctx, worst_e->arena_seg);
        if (s != PG_OK) return s;
    }

    return PG_OK;
}

/* ── Extended compaction features ────────────────────────── */

/*
 * Compute compaction statistics.
 */
typedef struct pg_compact_stats {
    uint32_t nodes_relocated;
    uint32_t edges_relocated;
    uint32_t segments_freed;
    float    fragmentation_before;
    float    fragmentation_after;
    uint32_t bytes_reclaimed;
} pg_compact_stats;

void pg_compact_get_stats(pg_compact_ctx *ctx, pg_compact_stats *stats) {
    stats->nodes_relocated = ctx->nodes_relocated;
    stats->edges_relocated = ctx->edges_relocated;
    stats->segments_freed = ctx->segments_freed;
    stats->fragmentation_before = 0.0f;
    stats->fragmentation_after = 0.0f;
    stats->bytes_reclaimed = ctx->segments_freed *
                              ctx->graph->node_arena.segment_capacity *
                              ctx->graph->node_arena.slot_size;
}

/*
 * Check if compaction is worthwhile based on current fragmentation.
 */
bool pg_compact_should_run(pg_graph *g) {
    float node_util = pg_arena_utilization(&g->node_arena);
    float edge_util = pg_arena_utilization(&g->edge_arena);
    return node_util < PG_COMPACT_THRESHOLD ||
           edge_util < PG_COMPACT_THRESHOLD;
}

/*
 * Dry-run compaction: compute what would change without actually
 * relocating anything.
 */
pg_compact_stats pg_compact_analyze(pg_graph *g) {
    pg_compact_stats stats = {0};

    /* analyze node arena */
    pg_arena_segment *seg = g->node_arena.segments;
    while (seg) {
        if (seg->slot_count > 0) {
            float frag = 1.0f - (float)seg->live / (float)seg->slot_count;
            if (frag > PG_COMPACT_THRESHOLD) {
                stats.nodes_relocated += seg->live;
                stats.segments_freed++;
                stats.bytes_reclaimed += (seg->slot_count - seg->live) *
                                          seg->slot_size;
            }
        }
        seg = seg->next;
    }

    /* analyze edge arena */
    seg = g->edge_arena.segments;
    while (seg) {
        if (seg->slot_count > 0) {
            float frag = 1.0f - (float)seg->live / (float)seg->slot_count;
            if (frag > PG_COMPACT_THRESHOLD) {
                stats.edges_relocated += seg->live;
                stats.segments_freed++;
            }
        }
        seg = seg->next;
    }

    return stats;
}
