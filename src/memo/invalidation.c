#include "invalidation.h"
#include <stdlib.h>

pg_status pg_invalidation_init(pg_invalidation_mgr *mgr) {
    mgr->callbacks = NULL;
    mgr->count = 0;
    return PG_OK;
}

void pg_invalidation_free(pg_invalidation_mgr *mgr) {
    pg_invalidation_cb *cb = mgr->callbacks;
    while (cb) {
        pg_invalidation_cb *next = cb->next;
        free(cb);
        cb = next;
    }
    mgr->callbacks = NULL;
    mgr->count = 0;
}

pg_status pg_invalidation_register(pg_invalidation_mgr *mgr,
                                    pg_invalidate_fn fn, void *ctx) {
    pg_invalidation_cb *cb = malloc(sizeof(pg_invalidation_cb));
    if (!cb) return PG_ERR_NOMEM;
    cb->fn = fn;
    cb->ctx = ctx;
    cb->next = mgr->callbacks;
    mgr->callbacks = cb;
    mgr->count++;
    return PG_OK;
}

void pg_invalidation_notify(pg_invalidation_mgr *mgr, pg_generation gen) {
    pg_invalidation_cb *cb = mgr->callbacks;
    while (cb) {
        cb->fn(cb->ctx, gen);
        cb = cb->next;
    }
}

/* ── Extended invalidation operations ────────────────────── */

/*
 * Count registered callbacks.
 */
uint32_t pg_invalidation_count(const pg_invalidation_mgr *mgr) {
    return mgr->count;
}

/*
 * Remove all callbacks.
 */
void pg_invalidation_clear(pg_invalidation_mgr *mgr) {
    pg_invalidation_cb *cb = mgr->callbacks;
    while (cb) {
        pg_invalidation_cb *next = cb->next;
        free(cb);
        cb = next;
    }
    mgr->callbacks = NULL;
    mgr->count = 0;
}

/*
 * Remove a specific callback by context pointer.
 */
bool pg_invalidation_unregister(pg_invalidation_mgr *mgr, void *ctx) {
    pg_invalidation_cb **pp = &mgr->callbacks;
    while (*pp) {
        if ((*pp)->ctx == ctx) {
            pg_invalidation_cb *dead = *pp;
            *pp = dead->next;
            free(dead);
            mgr->count--;
            return true;
        }
        pp = &(*pp)->next;
    }
    return false;
}
