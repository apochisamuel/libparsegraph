#ifndef PG_LABEL_INDEX_H
#define PG_LABEL_INDEX_H

#include "parsegraph/types.h"
#include "hash.h"
#include "../util/vector.h"

/*
 * Label index: maps label_id → vector of node_ids.
 * Supports scan iteration over all nodes with a given label.
 */

typedef struct pg_label_index {
    pg_hash_table  table;     /* label_id → pg_vector* of node_ids */
    uint32_t       total_entries;
} pg_label_index;

typedef struct pg_label_scan {
    pg_vector   *nodes;       /* pointer into index's vector */
    uint32_t     pos;
    pg_label_id  label;
} pg_label_scan;

pg_status pg_label_index_init(pg_label_index *idx);
void      pg_label_index_free(pg_label_index *idx);
pg_status pg_label_index_insert(pg_label_index *idx, pg_label_id label,
                                 pg_node_id node);
pg_status pg_label_index_remove(pg_label_index *idx, pg_label_id label,
                                 pg_node_id node);

/* scan: returns a scan state for iterating nodes with given label */
pg_status   pg_label_scan_begin(pg_label_index *idx, pg_label_id label,
                                 pg_label_scan *scan);
bool        pg_label_scan_next(pg_label_scan *scan, pg_node_id *out);
void        pg_label_scan_end(pg_label_scan *scan);

#endif /* PG_LABEL_INDEX_H */
