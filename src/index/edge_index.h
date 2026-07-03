#ifndef PG_EDGE_INDEX_H
#define PG_EDGE_INDEX_H

#include "parsegraph/types.h"
#include "hash.h"

typedef struct pg_edge_index {
    pg_hash_table table;   /* edge_type → vector of edge_ids */
    uint32_t total_entries;
} pg_edge_index;

pg_status pg_edge_index_init(pg_edge_index *idx);
void      pg_edge_index_free(pg_edge_index *idx);
pg_status pg_edge_index_insert(pg_edge_index *idx, pg_label_id type,
                                pg_edge_id eid);
pg_status pg_edge_index_remove(pg_edge_index *idx, pg_label_id type,
                                pg_edge_id eid);
uint32_t  pg_edge_index_count(pg_edge_index *idx, pg_label_id type);

#endif /* PG_EDGE_INDEX_H */
