#ifndef PG_JOIN_H
#define PG_JOIN_H

#include "parsegraph/types.h"
#include "matcher.h"
#include "../util/map.h"

typedef struct pg_join_ctx {
    pg_map       build_table;
    pg_node_id   join_key;
    uint32_t     build_count;
    uint32_t     probe_count;
    uint32_t     output_count;
} pg_join_ctx;

pg_status pg_join_init(pg_join_ctx *jc);
void      pg_join_free(pg_join_ctx *jc);
pg_status pg_join_build(pg_join_ctx *jc, pg_match_result *results);
pg_status pg_join_probe(pg_join_ctx *jc, pg_match_result *probe,
                         pg_match_result *output);

#endif
