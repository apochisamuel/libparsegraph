#ifndef PG_GENERATION_H
#define PG_GENERATION_H

#include "parsegraph/types.h"

typedef struct pg_gen_tracker {
    pg_generation current;
} pg_gen_tracker;

static inline void pg_gen_init(pg_gen_tracker *g) { g->current = 1; }
static inline pg_generation pg_gen_current(const pg_gen_tracker *g) {
    return g->current;
}
static inline pg_generation pg_gen_advance(pg_gen_tracker *g) {
    return ++(g->current);
}
static inline bool pg_gen_is_visible(pg_generation obj_gen,
                                      pg_generation view_gen) {
    return obj_gen <= view_gen;
}

#endif /* PG_GENERATION_H */
