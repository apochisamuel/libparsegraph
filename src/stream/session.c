#include "session.h"
#include "parsegraph/graph.h"
#include "../query/parser.h"
#include "../query/planner.h"
#include "../query/optimizer.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_session_create(pg_session **out, pg_session_id id,
                              pg_graph *g, pg_memo_cache *cache) {
    pg_session *s = calloc(1, sizeof(pg_session));
    if (!s) return PG_ERR_NOMEM;
    s->id = id;
    s->graph = g;
    s->cache = cache;
    pg_match_result_init(&s->last_result);
    s->active_executor = NULL;
    s->open = true;
    s->query_count = 0;
    s->mutation_count = 0;
    *out = s;
    return PG_OK;
}

/*
 * BUG-7 FREE SITE:
 * Cleanup frees the session context including result buffers.
 * If a transaction holds a pointer to this session (txn->session),
 * txn_commit will dereference freed memory.
 *
 * The missing abstraction: active_txn_count + session_acquire/release
 * so cleanup waits until all transactions have completed.
 */
void pg_session_cleanup(pg_session *sess) {
    if (!sess) return;
    sess->open = false;
    pg_match_result_free(&sess->last_result);
    if (sess->active_executor) {
        pg_executor_destroy(sess->active_executor);
        sess->active_executor = NULL;
    }
    free(sess);                          /* ← free site */
}

bool pg_session_is_open(const pg_session *sess) {
    return sess && sess->open;
}

pg_status pg_session_execute_query(pg_session *sess, pg_query_plan *plan) {
    if (!sess->open) return PG_ERR_SESSION_CLOSED;

    pg_executor *exec = NULL;
    pg_status s = pg_executor_create(&exec, sess->graph, plan);
    if (s != PG_OK) return s;

    s = pg_executor_run(exec);
    if (s != PG_OK) {
        pg_executor_destroy(exec);
        return s;
    }

    /* cache the result */
    uint64_t cache_key = (uint64_t)sess->query_count;
    pg_cache_store(sess->cache, cache_key, &exec->result,
                    pg_graph_generation(sess->graph));

    /* save last result */
    pg_match_result_free(&sess->last_result);
    pg_match_result_init(&sess->last_result);
    for (uint32_t i = 0; i < exec->result.node_count; i++) {
        pg_match_result_add_node(&sess->last_result, exec->result.nodes[i]);
    }

    sess->active_executor = exec;
    sess->query_count++;
    return PG_OK;
}

/* ── Extended session management ─────────────────────────── */

/*
 * Execute a raw query from binary input within a session.
 * Parses, plans, optimizes, and executes in one step.
 */
pg_status pg_session_execute_raw(pg_session *sess, const uint8_t *data,
                                  uint32_t len) {
    if (!sess->open) return PG_ERR_SESSION_CLOSED;

    pg_parse_ctx pctx = {
        .input = data,
        .len = len,
        .pos = 0,
        .intern_label = NULL,
        .intern_ctx = NULL
    };
    pg_ast_node *ast = NULL;
    pg_status s = pg_parse_query(&pctx, &ast);
    if (s != PG_OK || !ast) return s;

    pg_query_plan *plan = NULL;
    s = pg_planner_create_plan(ast, &plan);
    if (s != PG_OK) { pg_ast_free(ast); return s; }

    pg_optimizer opt;
    pg_optimizer_init(&opt, plan);
    pg_optimizer_run(&opt);

    s = pg_session_execute_query(sess, plan);

    pg_planner_free_plan(plan);
    pg_ast_free(ast);
    return s;
}

/*
 * Session statistics.
 */
typedef struct pg_session_stats {
    pg_session_id id;
    uint32_t query_count;
    uint32_t mutation_count;
    uint32_t cache_hits;
    uint32_t cache_misses;
    bool     open;
} pg_session_stats;

void pg_session_get_stats(pg_session *sess, pg_session_stats *stats) {
    if (!sess) return;
    stats->id = sess->id;
    stats->query_count = sess->query_count;
    stats->mutation_count = sess->mutation_count;
    stats->cache_hits = sess->cache ? sess->cache->hit_count : 0;
    stats->cache_misses = sess->cache ? sess->cache->miss_count : 0;
    stats->open = sess->open;
}

/*
 * Reset session state without closing it.
 * Clears the last result and destroys the active executor.
 */
pg_status pg_session_reset(pg_session *sess) {
    if (!sess || !sess->open) return PG_ERR_SESSION_CLOSED;

    pg_match_result_free(&sess->last_result);
    pg_match_result_init(&sess->last_result);

    if (sess->active_executor) {
        pg_executor_destroy(sess->active_executor);
        sess->active_executor = NULL;
    }

    return PG_OK;
}
