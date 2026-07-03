#ifndef PG_PROPERTIES_H
#define PG_PROPERTIES_H

#include "parsegraph/types.h"

pg_status    pg_props_init(pg_prop_store *ps);
void         pg_props_free(pg_prop_store *ps);
pg_status    pg_props_set(pg_prop_store *ps, pg_prop_key key,
                           const pg_value *val);
pg_value    *pg_props_get(pg_prop_store *ps, pg_prop_key key);
pg_status    pg_props_remove(pg_prop_store *ps, pg_prop_key key);
uint32_t     pg_props_count(const pg_prop_store *ps);

/* deep copy a value */
pg_status    pg_value_copy(pg_value *dst, const pg_value *src);
void         pg_value_free(pg_value *v);
bool         pg_value_equals(const pg_value *a, const pg_value *b);
uint32_t     pg_value_heap_size(const pg_value *v);

#endif /* PG_PROPERTIES_H */
