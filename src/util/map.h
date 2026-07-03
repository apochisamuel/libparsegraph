#ifndef PG_MAP_H
#define PG_MAP_H

#include "parsegraph/types.h"

typedef struct pg_map_entry {
    uint64_t key;
    void    *value;
    uint8_t  occupied;
} pg_map_entry;

typedef struct pg_map {
    pg_map_entry *buckets;
    uint32_t      capacity;
    uint32_t      count;
    uint32_t      mask;
} pg_map;

pg_status pg_map_init(pg_map *m, uint32_t initial_cap);
void      pg_map_free(pg_map *m);
pg_status pg_map_put(pg_map *m, uint64_t key, void *value);
void     *pg_map_get(pg_map *m, uint64_t key);
bool      pg_map_has(pg_map *m, uint64_t key);
void     *pg_map_remove(pg_map *m, uint64_t key);
pg_status pg_map_grow(pg_map *m);
uint32_t  pg_map_count(const pg_map *m);

typedef void (*pg_map_iter_fn)(uint64_t key, void *value, void *ctx);
void pg_map_foreach(pg_map *m, pg_map_iter_fn fn, void *ctx);
void pg_map_clear(pg_map *m);

#endif /* PG_MAP_H */
