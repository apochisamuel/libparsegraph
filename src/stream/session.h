#ifndef PG_SESSION_H
#define PG_SESSION_H

#include "parsegraph/types.h"
#include "../exec/executor.h"
#include "../exec/matcher.h"
#include "../memo/cache.h"

struct pg_session {
    pg_session_id    id;
    pg_graph        *graph;
    pg_memo_cache   *cache;
    pg_match_result  last_result;
    pg_executor     *active_executor;
    bool             open;
    uint32_t         query_count;
    uint32_t         mutation_count;
};

pg_status  pg_session_create(pg_session **out, pg_session_id id,
                              pg_graph *g, pg_memo_cache *cache);
void       pg_session_cleanup(pg_session *sess);
bool       pg_session_is_open(const pg_session *sess);
pg_status  pg_session_execute_query(pg_session *sess, pg_query_plan *plan);

#endif
