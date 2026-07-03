#include "parsegraph/graph.h"
#include "../src/stream/session.h"
#include "../src/stream/txn.h"
#include "../src/memo/cache.h"
#include "../src/query/parser.h"
#include "../src/query/planner.h"
#include <assert.h>

int test_session(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);
    pg_memo_cache *cache;
    assert(pg_cache_create(&cache, 32) == PG_OK);

    pg_session *sess;
    assert(pg_session_create(&sess, 1, g, cache) == PG_OK);
    assert(pg_session_is_open(sess));

    pg_txn *txn;
    assert(pg_txn_begin(&txn, 1, sess) == PG_OK);
    assert(pg_txn_is_active(txn));
    assert(pg_txn_commit(txn) == PG_OK);

    pg_session_cleanup(sess);
    pg_cache_destroy(cache);
    pg_graph_destroy(g);
    return 0;
}

int test_session_multiple_queries(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);
    pg_memo_cache *cache;
    assert(pg_cache_create(&cache, 32) == PG_OK);

    pg_label_id lbl = pg_graph_intern_label(g, "Item", 4);
    for (int i = 0; i < 5; i++)
        pg_graph_add_node(g, lbl);

    pg_session *sess;
    assert(pg_session_create(&sess, 1, g, cache) == PG_OK);

    /* execute multiple queries */
    uint8_t q1[] = { 0x01, 0x00, 0x00, 0x00, 0x00 };
    pg_parse_ctx ctx1 = { .input = q1, .len = sizeof(q1), .pos = 0 };
    pg_ast_node *ast1 = NULL;
    assert(pg_parse_query(&ctx1, &ast1) == PG_OK);
    pg_query_plan *plan1 = NULL;
    assert(pg_planner_create_plan(ast1, &plan1) == PG_OK);
    assert(pg_session_execute_query(sess, plan1) == PG_OK);
    assert(sess->query_count == 1);

    pg_planner_free_plan(plan1);
    pg_ast_free(ast1);

    /* reset and execute another */
    pg_session_reset(sess);

    pg_session_cleanup(sess);
    pg_cache_destroy(cache);
    pg_graph_destroy(g);
    return 0;
}
