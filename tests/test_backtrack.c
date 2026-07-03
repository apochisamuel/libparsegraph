#include "parsegraph/graph.h"
#include "../src/exec/backtrack.h"
#include "../src/core/snapshot.h"
#include <assert.h>

int test_backtrack(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id lbl = pg_graph_intern_label(g, "X", 1);
    pg_node_id n1 = pg_graph_add_node(g, lbl);

    pg_snapshot *snap;
    assert(pg_graph_snapshot(g, &snap) == PG_OK);

    pg_backtrack_stack bs;
    pg_backtrack_init(&bs);

    pg_cursor cur;
    pg_cursor_init(&cur, g);
    pg_cursor_seek_node(&cur, n1);

    pg_match_result mr;
    pg_match_result_init(&mr);

    pg_backtrack_save(&bs, snap, &cur, 0, &mr);
    assert(pg_backtrack_has_points(&bs));

    uint32_t depth;
    pg_backtrack_restore(&bs, &cur, &depth, &mr);
    assert(!pg_backtrack_has_points(&bs));

    pg_cursor_close(&cur);
    pg_match_result_free(&mr);
    pg_backtrack_free(&bs);
    pg_snapshot_release(snap);
    pg_graph_destroy(g);
    return 0;
}

int test_backtrack_multiple(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id lbl = pg_graph_intern_label(g, "A", 1);
    pg_node_id n1 = pg_graph_add_node(g, lbl);
    pg_node_id n2 = pg_graph_add_node(g, lbl);
    pg_node_id n3 = pg_graph_add_node(g, lbl);

    pg_snapshot *snap;
    assert(pg_graph_snapshot(g, &snap) == PG_OK);

    pg_backtrack_stack bs;
    pg_backtrack_init(&bs);

    pg_cursor cur;
    pg_cursor_init(&cur, g);
    pg_match_result mr;
    pg_match_result_init(&mr);

    /* save multiple checkpoints */
    pg_cursor_seek_node(&cur, n1);
    pg_backtrack_save(&bs, snap, &cur, 0, &mr);
    pg_cursor_seek_node(&cur, n2);
    pg_backtrack_save(&bs, snap, &cur, 1, &mr);
    pg_cursor_seek_node(&cur, n3);
    pg_backtrack_save(&bs, snap, &cur, 2, &mr);

    assert(pg_backtrack_count(&bs) == 3);

    /* restore in LIFO order */
    uint32_t depth;
    pg_backtrack_restore(&bs, &cur, &depth, &mr);
    assert(depth == 2);
    pg_backtrack_restore(&bs, &cur, &depth, &mr);
    assert(depth == 1);

    assert(pg_backtrack_count(&bs) == 1);

    pg_backtrack_clear(&bs);
    assert(!pg_backtrack_has_points(&bs));

    pg_cursor_close(&cur);
    pg_match_result_free(&mr);
    pg_backtrack_free(&bs);
    pg_snapshot_release(snap);
    pg_graph_destroy(g);
    return 0;
}
