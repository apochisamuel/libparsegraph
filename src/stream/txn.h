#ifndef PG_TXN_H
#define PG_TXN_H

#include "parsegraph/types.h"
#include "session.h"
#include "../storage/journal.h"

struct pg_txn {
    pg_txn_id     id;
    pg_session   *session;       /* BUG-7 ALIAS: raw pointer to session */
    pg_journal    journal;
    pg_generation begin_gen;
    bool          active;
    uint32_t      op_count;
};

pg_status pg_txn_begin(pg_txn **out, pg_txn_id id, pg_session *sess);
pg_status pg_txn_commit(pg_txn *txn);
pg_status pg_txn_abort(pg_txn *txn);
bool      pg_txn_is_active(const pg_txn *txn);

#endif
