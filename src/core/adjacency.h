#ifndef PG_ADJACENCY_H
#define PG_ADJACENCY_H

#include "parsegraph/types.h"

pg_status pg_adj_init(pg_adj_list *al);
void      pg_adj_free(pg_adj_list *al);
pg_status pg_adj_append(pg_adj_list *al, pg_edge_id eid);
pg_status pg_adj_remove(pg_adj_list *al, pg_edge_id eid);
bool      pg_adj_contains(const pg_adj_list *al, pg_edge_id eid);
pg_edge_id pg_adj_get(const pg_adj_list *al, uint32_t idx);

#endif /* PG_ADJACENCY_H */
