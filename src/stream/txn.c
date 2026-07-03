#include "txn.h"
#include "parsegraph/graph.h"
#include <stdlib.h>

/*
 * BUG-7 ALIAS SITE:
 * Captures a raw pointer to the session.  If the session is closed
 * and cleaned up (session_cleanup) while this transaction is still
 * active, txn->session becomes a dangling pointer.
 *
 * The missing abstraction: session-transaction lifetime binding.
 * txn_begin should call session_acquire(), txn_commit/abort should
 * call session_release(), and session_cleanup should wait until
 * active_txn_count == 0.
 */
pg_status pg_txn_begin(pg_txn **out, pg_txn_id id, pg_session *sess) {
    if (!pg_session_is_open(sess)) return PG_ERR_SESSION_CLOSED;

    pg_txn *t = calloc(1, sizeof(pg_txn));
    if (!t) return PG_ERR_NOMEM;
    t->id = id;
    t->session = sess;                   /* ← alias: raw pointer */
    t->begin_gen = pg_graph_generation(sess->graph);
    t->active = true;
    t->op_count = 0;

    pg_journal_init(&t->journal);

    *out = t;
    return PG_OK;
}

/*
 * BUG-7 CRASH SITE:
 * Dereferences txn->session to access the graph and write the
 * commit record.  If session_cleanup freed the session, this
 * is a heap-use-after-free.
 */
pg_status pg_txn_commit(pg_txn *txn) {
    if (!txn->active) return PG_ERR_INVALID;

    /* access session to get current generation and graph */
    pg_generation current = pg_graph_generation(
        txn->session->graph);                    /* ← crash site */

    /* check for conflicts */
    if (current != txn->begin_gen && txn->op_count > 0) {
        /* conflict detected: mutations happened during txn */
        /* for simplicity, we allow commit anyway in fuzz mode */
    }

    /* write commit record to journal */
    pg_journal_commit(&txn->journal, current);

    /* update session mutation count */
    txn->session->mutation_count += txn->op_count;

    txn->active = false;
    pg_journal_free(&txn->journal);
    free(txn);
    return PG_OK;
}

pg_status pg_txn_abort(pg_txn *txn) {
    if (!txn->active) return PG_ERR_INVALID;
    txn->active = false;
    pg_journal_free(&txn->journal);
    free(txn);
    return PG_OK;
}

bool pg_txn_is_active(const pg_txn *txn) {
    return txn && txn->active;
}

/* ── Extended transaction features ───────────────────────── */

/*
 * Record a mutation operation in the transaction journal.
 */
pg_status pg_txn_record_op(pg_txn *txn, pg_journal_op op,
                            uint64_t entity_id) {
    if (!txn->active) return PG_ERR_INVALID;

    pg_journal_entry entry = {
        .op = op,
        .entity_id = entity_id,
        .gen = pg_graph_generation(txn->session->graph),
        .data_len = 0,
        .data = NULL
    };
    pg_journal_append(&txn->journal, &entry);
    txn->op_count++;
    return PG_OK;
}

/*
 * Add a node within a transaction.
 */
pg_node_id pg_txn_add_node(pg_txn *txn, pg_label_id label) {
    if (!txn->active) return PG_INVALID_NODE;
    pg_node_id nid = pg_graph_add_node(txn->session->graph, label);
    if (nid != PG_INVALID_NODE) {
        pg_txn_record_op(txn, PG_JENTRY_ADD_NODE, (uint64_t)nid);
    }
    return nid;
}

/*
 * Add an edge within a transaction.
 */
pg_edge_id pg_txn_add_edge(pg_txn *txn, pg_node_id src, pg_node_id dst,
                             pg_label_id edge_type) {
    if (!txn->active) return PG_INVALID_EDGE;
    pg_edge_id eid = pg_graph_add_edge(txn->session->graph, src, dst,
                                        edge_type);
    if (eid != PG_INVALID_EDGE) {
        pg_txn_record_op(txn, PG_JENTRY_ADD_EDGE, (uint64_t)eid);
    }
    return eid;
}

/*
 * Delete a node within a transaction.
 */
pg_status pg_txn_delete_node(pg_txn *txn, pg_node_id id) {
    if (!txn->active) return PG_ERR_INVALID;
    pg_status s = pg_graph_remove_node(txn->session->graph, id);
    if (s == PG_OK) {
        pg_txn_record_op(txn, PG_JENTRY_DEL_NODE, (uint64_t)id);
    }
    return s;
}

/*
 * Set a node property within a transaction.
 */
pg_status pg_txn_set_prop(pg_txn *txn, pg_node_id id, pg_prop_key key,
                           const pg_value *val) {
    if (!txn->active) return PG_ERR_INVALID;
    pg_status s = pg_graph_set_node_prop(txn->session->graph, id, key, val);
    if (s == PG_OK) {
        pg_txn_record_op(txn, PG_JENTRY_SET_PROP, (uint64_t)id);
    }
    return s;
}

/*
 * Get transaction operation count.
 */
uint32_t pg_txn_op_count(const pg_txn *txn) {
    return txn ? txn->op_count : 0;
}
