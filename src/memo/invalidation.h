#ifndef PG_INVALIDATION_H
#define PG_INVALIDATION_H

#include "parsegraph/types.h"

typedef void (*pg_invalidate_fn)(void *ctx, pg_generation gen);

typedef struct pg_invalidation_cb {
    pg_invalidate_fn fn;
    void *ctx;
    struct pg_invalidation_cb *next;
} pg_invalidation_cb;

typedef struct pg_invalidation_mgr {
    pg_invalidation_cb *callbacks;
    uint32_t count;
} pg_invalidation_mgr;

pg_status pg_invalidation_init(pg_invalidation_mgr *mgr);
void      pg_invalidation_free(pg_invalidation_mgr *mgr);
pg_status pg_invalidation_register(pg_invalidation_mgr *mgr,
                                    pg_invalidate_fn fn, void *ctx);
void      pg_invalidation_notify(pg_invalidation_mgr *mgr,
                                  pg_generation gen);

#endif
