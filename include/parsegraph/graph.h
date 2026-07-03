#ifndef PARSEGRAPH_GRAPH_H
#define PARSEGRAPH_GRAPH_H

#include "types.h"

/* lifecycle */
pg_status    pg_graph_create(pg_graph **out);
void         pg_graph_destroy(pg_graph *g);

/* nodes */
pg_node_id   pg_graph_add_node(pg_graph *g, pg_label_id label);
pg_status    pg_graph_remove_node(pg_graph *g, pg_node_id id);
pg_node     *pg_graph_get_node(pg_graph *g, pg_node_id id);
uint32_t     pg_graph_node_count(const pg_graph *g);

/* edges */
pg_edge_id   pg_graph_add_edge(pg_graph *g, pg_node_id src, pg_node_id dst,
                                pg_label_id edge_type);
pg_status    pg_graph_remove_edge(pg_graph *g, pg_edge_id id);
pg_edge     *pg_graph_get_edge(pg_graph *g, pg_edge_id id);
uint32_t     pg_graph_edge_count(const pg_graph *g);

/* properties */
pg_status    pg_graph_set_node_prop(pg_graph *g, pg_node_id id,
                                    pg_prop_key key, const pg_value *val);
pg_value    *pg_graph_get_node_prop(pg_graph *g, pg_node_id id,
                                    pg_prop_key key);

/* labels */
pg_label_id  pg_graph_intern_label(pg_graph *g, const char *name,
                                    uint32_t len);
const char  *pg_graph_resolve_label(pg_graph *g, pg_label_id id);

/* generation */
pg_generation pg_graph_generation(const pg_graph *g);
uint32_t      pg_graph_mutation_count(const pg_graph *g);

/* snapshot */
pg_status    pg_graph_snapshot(pg_graph *g, pg_snapshot **out);

/* arena access for compaction */
struct pg_arena *pg_graph_node_arena(pg_graph *g);
struct pg_arena *pg_graph_edge_arena(pg_graph *g);

#endif /* PARSEGRAPH_GRAPH_H */
