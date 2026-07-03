#include "../src/index/label_index.h"
#include "../src/index/prop_index.h"
#include "../src/index/edge_index.h"
#include <assert.h>

int test_index(void) {
    pg_label_index idx;
    assert(pg_label_index_init(&idx) == PG_OK);

    assert(pg_label_index_insert(&idx, 1, 100) == PG_OK);
    assert(pg_label_index_insert(&idx, 1, 200) == PG_OK);
    assert(pg_label_index_insert(&idx, 2, 300) == PG_OK);

    pg_label_scan scan;
    pg_label_scan_begin(&idx, 1, &scan);
    pg_node_id nid;
    int count = 0;
    while (pg_label_scan_next(&scan, &nid)) count++;
    assert(count == 2);
    pg_label_scan_end(&scan);

    assert(pg_label_index_remove(&idx, 1, 100) == PG_OK);
    pg_label_index_free(&idx);
    return 0;
}

int test_index_prop(void) {
    pg_prop_index idx;
    assert(pg_prop_index_init(&idx) == PG_OK);

    pg_value v1 = { .type = PG_VAL_INT, .v.i = 42 };
    pg_value v2 = { .type = PG_VAL_INT, .v.i = 99 };

    assert(pg_prop_index_insert(&idx, 1, &v1, 100) == PG_OK);
    assert(pg_prop_index_insert(&idx, 1, &v1, 200) == PG_OK);
    assert(pg_prop_index_insert(&idx, 2, &v2, 300) == PG_OK);

    assert(pg_prop_index_count_key(&idx, 1) == 2);
    assert(pg_prop_index_count_key(&idx, 2) == 1);

    pg_prop_index_free(&idx);
    return 0;
}

int test_index_edge(void) {
    pg_edge_index idx;
    assert(pg_edge_index_init(&idx) == PG_OK);

    assert(pg_edge_index_insert(&idx, 1, 100) == PG_OK);
    assert(pg_edge_index_insert(&idx, 1, 200) == PG_OK);
    assert(pg_edge_index_insert(&idx, 2, 300) == PG_OK);

    assert(pg_edge_index_count(&idx, 1) == 2);
    assert(pg_edge_index_count(&idx, 2) == 1);
    assert(pg_edge_index_count(&idx, 3) == 0);

    pg_edge_index_free(&idx);
    return 0;
}
