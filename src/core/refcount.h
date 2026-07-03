#ifndef PG_REFCOUNT_H
#define PG_REFCOUNT_H

#include "parsegraph/types.h"

static inline void pg_ref_inc(uint32_t *rc) { (*rc)++; }

static inline bool pg_ref_dec(uint32_t *rc) {
    if (*rc == 0) return true;
    (*rc)--;
    return *rc == 0;
}

static inline uint32_t pg_ref_get(const uint32_t *rc) { return *rc; }

#endif /* PG_REFCOUNT_H */
