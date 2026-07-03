#ifndef PG_MATCHER_H
#define PG_MATCHER_H

#include "parsegraph/types.h"
#include "cursor.h"
#include "../query/ast.h"

typedef struct pg_match_result {
    pg_node   **nodes;
    pg_edge   **edges;
    uint32_t    node_count;
    uint32_t    edge_count;
    uint32_t    capacity;
} pg_match_result;

pg_status pg_match_result_init(pg_match_result *mr);
void      pg_match_result_free(pg_match_result *mr);
pg_status pg_match_result_add_node(pg_match_result *mr, pg_node *n);
pg_status pg_match_result_add_edge(pg_match_result *mr, pg_edge *e);

bool      pg_match_node_predicate(pg_cursor *cur, pg_ast_node *pattern);
bool      pg_match_edge_predicate(pg_graph *g, pg_edge_id eid,
                                   pg_ast_node *edge_pat);
pg_status pg_match_pattern(pg_graph *g, pg_cursor *cur,
                            pg_ast_node *pattern, pg_match_result *result);

#endif
