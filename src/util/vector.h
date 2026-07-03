#ifndef PG_VECTOR_H
#define PG_VECTOR_H

#include "parsegraph/types.h"

typedef struct pg_vector {
    void    *data;
    size_t   elem_size;
    uint32_t count;
    uint32_t capacity;
} pg_vector;

pg_status pg_vector_init(pg_vector *v, size_t elem_size, uint32_t initial_cap);
void      pg_vector_free(pg_vector *v);
pg_status pg_vector_push(pg_vector *v, const void *elem);
void     *pg_vector_get(pg_vector *v, uint32_t idx);
void     *pg_vector_last(pg_vector *v);
pg_status pg_vector_pop(pg_vector *v, void *out);
void      pg_vector_clear(pg_vector *v);
pg_status pg_vector_remove(pg_vector *v, uint32_t idx);
pg_status pg_vector_ensure(pg_vector *v, uint32_t needed);
void     *pg_vector_raw(pg_vector *v);
uint32_t  pg_vector_count(const pg_vector *v);

#endif /* PG_VECTOR_H */
