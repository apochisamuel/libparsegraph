#ifndef PG_GRAPH_INTERNAL_H
#define PG_GRAPH_INTERNAL_H

#include "parsegraph/types.h"
#include "arena.h"
#include "generation.h"
#include "labels.h"
#include "properties.h"
#include "adjacency.h"
#include "snapshot.h"
#include "../util/map.h"

struct pg_graph {
    pg_arena        node_arena;
    pg_arena        edge_arena;
    pg_map          node_map;       /* id → pg_node* */
    pg_map          edge_map;       /* id → pg_edge* */
    pg_label_store  labels;
    pg_gen_tracker  generation;
    pg_node_id      next_node_id;
    pg_edge_id      next_edge_id;
    uint32_t        node_count;
    uint32_t        edge_count;
    uint32_t        mutation_count; /* mutations since last compaction */
};

#endif /* PG_GRAPH_INTERNAL_H */
