#ifndef PG_STRING_INTERN_H
#define PG_STRING_INTERN_H

#include "parsegraph/types.h"

typedef struct pg_intern_entry {
    char    *str;
    uint32_t len;
    uint32_t id;
    uint32_t hash;
    struct pg_intern_entry *next;
} pg_intern_entry;

typedef struct pg_string_intern {
    pg_intern_entry **buckets;
    uint32_t          bucket_count;
    uint32_t          count;
    uint32_t          next_id;
} pg_string_intern;

pg_status   pg_intern_init(pg_string_intern *si, uint32_t initial_buckets);
void        pg_intern_free(pg_string_intern *si);
bool        pg_intern_contains(pg_string_intern *si, const char *str, uint32_t len);
uint32_t    pg_intern_put(pg_string_intern *si, const char *str, uint32_t len);
const char *pg_intern_get(pg_string_intern *si, uint32_t id);
uint32_t    pg_intern_find(pg_string_intern *si, const char *str, uint32_t len);

#endif /* PG_STRING_INTERN_H */
