#ifndef PARSEGRAPH_QUERY_H
#define PARSEGRAPH_QUERY_H

#include "types.h"

/* Public query API */
pg_status pg_query_execute(pg_graph *g, const uint8_t *query_data,
                            uint32_t query_len);

#endif
