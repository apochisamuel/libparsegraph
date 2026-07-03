#ifndef PG_PROP_INDEX_H
#define PG_PROP_INDEX_H

#include "parsegraph/types.h"
#include "hash.h"

typedef struct pg_prop_index {
    pg_hash_table table;    /* compound key (prop_key << 32 | hash(value)) → vector */
    uint32_t total_entries;
} pg_prop_index;

pg_status pg_prop_index_init(pg_prop_index *idx);
void      pg_prop_index_free(pg_prop_index *idx);
pg_status pg_prop_index_insert(pg_prop_index *idx, pg_prop_key key,
                                const pg_value *val, pg_node_id node);
pg_status pg_prop_index_remove(pg_prop_index *idx, pg_prop_key key,
                                const pg_value *val, pg_node_id node);

#endif /* PG_PROP_INDEX_H */
