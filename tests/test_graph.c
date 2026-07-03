#include "parsegraph/graph.h"
#include "../src/core/snapshot.h"
#include <assert.h>
#include <string.h>

int test_graph(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);
    assert(pg_graph_node_count(g) == 0);

    pg_label_id lbl = pg_graph_intern_label(g, "Person", 6);
    assert(lbl != PG_INVALID_LABEL);

    pg_node_id n1 = pg_graph_add_node(g, lbl);
    pg_node_id n2 = pg_graph_add_node(g, lbl);
    assert(n1 != PG_INVALID_NODE && n2 != PG_INVALID_NODE);
    assert(pg_graph_node_count(g) == 2);

    pg_value val = { .type = PG_VAL_INT, .v.i = 42 };
    assert(pg_graph_set_node_prop(g, n1, 1, &val) == PG_OK);
    pg_value *pv = pg_graph_get_node_prop(g, n1, 1);
    assert(pv && pv->v.i == 42);

    pg_label_id etype = pg_graph_intern_label(g, "KNOWS", 5);
    pg_edge_id e1 = pg_graph_add_edge(g, n1, n2, etype);
    assert(e1 != PG_INVALID_EDGE);
    assert(pg_graph_edge_count(g) == 1);

    assert(pg_graph_remove_node(g, n2) == PG_OK);
    assert(pg_graph_node_count(g) == 1);
    assert(pg_graph_edge_count(g) == 0);

    pg_graph_destroy(g);
    return 0;
}

int test_graph_edges(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id person = pg_graph_intern_label(g, "Person", 6);
    pg_label_id knows = pg_graph_intern_label(g, "KNOWS", 5);
    pg_label_id likes = pg_graph_intern_label(g, "LIKES", 5);

    pg_node_id a = pg_graph_add_node(g, person);
    pg_node_id b = pg_graph_add_node(g, person);
    pg_node_id c = pg_graph_add_node(g, person);

    pg_edge_id e1 = pg_graph_add_edge(g, a, b, knows);
    pg_edge_id e2 = pg_graph_add_edge(g, b, c, knows);
    pg_edge_id e3 = pg_graph_add_edge(g, a, c, likes);
    assert(e1 != PG_INVALID_EDGE);
    assert(e2 != PG_INVALID_EDGE);
    assert(e3 != PG_INVALID_EDGE);
    assert(pg_graph_edge_count(g) == 3);

    pg_edge *edge = pg_graph_get_edge(g, e1);
    assert(edge && edge->src == a && edge->dst == b);
    assert(edge->edge_type == knows);

    assert(pg_graph_remove_edge(g, e2) == PG_OK);
    assert(pg_graph_edge_count(g) == 2);

    pg_graph_destroy(g);
    return 0;
}

int test_graph_properties(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id lbl = pg_graph_intern_label(g, "Item", 4);
    pg_node_id n = pg_graph_add_node(g, lbl);

    pg_value v1 = { .type = PG_VAL_INT, .v.i = 100 };
    pg_value v2 = { .type = PG_VAL_BOOL, .v.b = true };
    pg_value v3 = { .type = PG_VAL_FLOAT, .v.f = 3.14 };

    assert(pg_graph_set_node_prop(g, n, 1, &v1) == PG_OK);
    assert(pg_graph_set_node_prop(g, n, 2, &v2) == PG_OK);
    assert(pg_graph_set_node_prop(g, n, 3, &v3) == PG_OK);

    pg_value *r1 = pg_graph_get_node_prop(g, n, 1);
    assert(r1 && r1->type == PG_VAL_INT && r1->v.i == 100);

    pg_value *r2 = pg_graph_get_node_prop(g, n, 2);
    assert(r2 && r2->type == PG_VAL_BOOL && r2->v.b == true);

    pg_value *r3 = pg_graph_get_node_prop(g, n, 3);
    assert(r3 && r3->type == PG_VAL_FLOAT);

    /* overwrite property */
    pg_value v4 = { .type = PG_VAL_INT, .v.i = 200 };
    assert(pg_graph_set_node_prop(g, n, 1, &v4) == PG_OK);
    pg_value *r4 = pg_graph_get_node_prop(g, n, 1);
    assert(r4 && r4->v.i == 200);

    pg_graph_destroy(g);
    return 0;
}

int test_graph_snapshot(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id lbl = pg_graph_intern_label(g, "Snap", 4);
    pg_node_id n1 = pg_graph_add_node(g, lbl);
    pg_node_id n2 = pg_graph_add_node(g, lbl);

    pg_snapshot *snap;
    assert(pg_graph_snapshot(g, &snap) == PG_OK);
    assert(snap);

    pg_node *resolved = pg_snapshot_resolve(snap, n1);
    assert(resolved && resolved->id == n1);

    pg_node *r2 = pg_snapshot_resolve(snap, n2);
    assert(r2 && r2->id == n2);

    assert(pg_snapshot_resolve(snap, 9999) == NULL);

    pg_snapshot_release(snap);
    pg_graph_destroy(g);
    return 0;
}

int test_graph_labels(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id l1 = pg_graph_intern_label(g, "Person", 6);
    pg_label_id l2 = pg_graph_intern_label(g, "City", 4);
    pg_label_id l3 = pg_graph_intern_label(g, "Person", 6);

    assert(l1 != PG_INVALID_LABEL);
    assert(l2 != PG_INVALID_LABEL);
    assert(l1 == l3);  /* same string → same ID */
    assert(l1 != l2);

    const char *name = pg_graph_resolve_label(g, l1);
    assert(name);
    assert(strcmp(name, "Person") == 0);

    pg_graph_destroy(g);
    return 0;
}
