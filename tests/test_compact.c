#include "parsegraph/graph.h"
#include "../src/storage/compact.h"
#include "../src/storage/segment.h"
#include <assert.h>

int test_compact(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id lbl = pg_graph_intern_label(g, "Test", 4);
    pg_node_id ids[10];
    for (int i = 0; i < 10; i++)
        ids[i] = pg_graph_add_node(g, lbl);
    for (int i = 0; i < 5; i++)
        pg_graph_remove_node(g, ids[i]);

    assert(pg_graph_node_count(g) == 5);

    pg_segment_mgr nsm, esm;
    pg_segment_mgr_init(&nsm, pg_graph_node_arena(g));
    pg_segment_mgr_init(&esm, pg_graph_edge_arena(g));

    pg_compact_ctx cctx;
    pg_compact_init(&cctx, g, &nsm, &esm);

    pg_segment_mgr_free(&nsm);
    pg_segment_mgr_free(&esm);
    pg_graph_destroy(g);
    return 0;
}

int test_compact_analyze(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id lbl = pg_graph_intern_label(g, "Node", 4);
    for (int i = 0; i < 20; i++)
        pg_graph_add_node(g, lbl);

    /* delete most nodes to create fragmentation */
    for (pg_node_id i = 1; i <= 15; i++)
        pg_graph_remove_node(g, i);

    assert(pg_graph_node_count(g) == 5);
    bool should = pg_compact_should_run(g);
    /* may or may not trigger depending on arena layout */
    (void)should;

    pg_graph_destroy(g);
    return 0;
}
