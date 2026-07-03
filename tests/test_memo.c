#include "../src/memo/cache.h"
#include "../src/exec/matcher.h"
#include <assert.h>

int test_memo(void) {
    pg_memo_cache *cache;
    assert(pg_cache_create(&cache, 16) == PG_OK);

    pg_match_result mr;
    pg_match_result_init(&mr);

    pg_cache_store(cache, 42, &mr, 1);
    pg_cache_entry *e = pg_cache_lookup(cache, 42);
    assert(e != NULL && e->key == 42);

    assert(pg_cache_lookup(cache, 99) == NULL);

    pg_cache_invalidate(cache, 2);

    pg_match_result_free(&mr);
    pg_cache_destroy(cache);
    return 0;
}

int test_memo_eviction(void) {
    pg_memo_cache *cache;
    assert(pg_cache_create(&cache, 4) == PG_OK);

    pg_match_result mr;
    pg_match_result_init(&mr);

    /* fill cache to capacity */
    for (uint64_t i = 0; i < 4; i++) {
        pg_cache_store(cache, i, &mr, 1);
    }

    /* storing one more should trigger eviction */
    pg_cache_store(cache, 99, &mr, 1);

    /* 99 should be there */
    pg_cache_entry *e = pg_cache_lookup(cache, 99);
    assert(e != NULL);

    pg_match_result_free(&mr);
    pg_cache_destroy(cache);
    return 0;
}

int test_memo_hit_rate(void) {
    pg_memo_cache *cache;
    assert(pg_cache_create(&cache, 16) == PG_OK);

    pg_match_result mr;
    pg_match_result_init(&mr);

    pg_cache_store(cache, 1, &mr, 1);
    pg_cache_lookup(cache, 1);  /* hit */
    pg_cache_lookup(cache, 1);  /* hit */
    pg_cache_lookup(cache, 2);  /* miss */

    float rate = pg_cache_hit_rate(cache);
    assert(rate > 0.5f);

    pg_match_result_free(&mr);
    pg_cache_destroy(cache);
    return 0;
}
