#include "../src/util/buffer.h"
#include "../src/util/vector.h"
#include "../src/util/map.h"
#include "../src/util/pool.h"
#include "../src/util/slab.h"
#include "../src/util/string_intern.h"
#include <assert.h>
#include <string.h>

int test_buffer_ops(void) {
    pg_buffer buf;
    assert(pg_buffer_init(&buf, 16) == PG_OK);

    uint8_t data[] = {1, 2, 3, 4, 5};
    assert(pg_buffer_append(&buf, data, 5) == PG_OK);
    assert(buf.len == 5);

    assert(pg_buffer_write_u8(&buf, 0xFF) == PG_OK);
    assert(buf.len == 6);

    assert(pg_buffer_write_u16(&buf, 0x1234) == PG_OK);
    assert(buf.len == 8);

    uint8_t val8;
    assert(pg_buffer_read_u8(&buf, 5, &val8));
    assert(val8 == 0xFF);

    uint16_t val16;
    assert(pg_buffer_read_u16(&buf, 6, &val16));
    assert(val16 == 0x1234);

    pg_buffer_consume(&buf, 3);
    assert(buf.len == 5);

    pg_buffer_free(&buf);
    return 0;
}

int test_vector_ops(void) {
    pg_vector v;
    assert(pg_vector_init(&v, sizeof(int), 4) == PG_OK);

    int vals[] = {5, 3, 1, 4, 2};
    for (int i = 0; i < 5; i++)
        assert(pg_vector_push(&v, &vals[i]) == PG_OK);
    assert(pg_vector_count(&v) == 5);

    int *first = pg_vector_get(&v, 0);
    assert(first && *first == 5);

    int *last = pg_vector_last(&v);
    assert(last && *last == 2);

    int popped;
    assert(pg_vector_pop(&v, &popped) == PG_OK);
    assert(popped == 2);
    assert(pg_vector_count(&v) == 4);

    assert(pg_vector_remove(&v, 0) == PG_OK);
    assert(pg_vector_count(&v) == 3);

    pg_vector_clear(&v);
    assert(pg_vector_count(&v) == 0);

    pg_vector_free(&v);
    return 0;
}

int test_map_ops(void) {
    pg_map m;
    assert(pg_map_init(&m, 16) == PG_OK);

    int val1 = 100, val2 = 200, val3 = 300;
    assert(pg_map_put(&m, 1, &val1) == PG_OK);
    assert(pg_map_put(&m, 2, &val2) == PG_OK);
    assert(pg_map_put(&m, 3, &val3) == PG_OK);
    assert(pg_map_count(&m) == 3);

    int *r = pg_map_get(&m, 2);
    assert(r && *r == 200);

    assert(pg_map_has(&m, 1));
    assert(!pg_map_has(&m, 99));

    pg_map_remove(&m, 2);
    assert(pg_map_count(&m) == 2);
    assert(!pg_map_has(&m, 2));

    pg_map_clear(&m);
    assert(pg_map_count(&m) == 0);

    pg_map_free(&m);
    return 0;
}

int test_pool_ops(void) {
    pg_pool p;
    assert(pg_pool_init(&p, 32, 8) == PG_OK);

    void *a = pg_pool_alloc(&p);
    void *b = pg_pool_alloc(&p);
    void *c = pg_pool_alloc(&p);
    assert(a && b && c);
    assert(pg_pool_active(&p) == 3);

    pg_pool_release(&p, b);
    assert(pg_pool_active(&p) == 2);

    void *d = pg_pool_alloc(&p);
    assert(d);
    assert(pg_pool_active(&p) == 3);

    pg_pool_destroy(&p);
    return 0;
}

int test_slab_ops(void) {
    pg_slab s;
    assert(pg_slab_init(&s, 24) == PG_OK);

    void *objs[10];
    for (int i = 0; i < 10; i++) {
        objs[i] = pg_slab_alloc(&s);
        assert(objs[i]);
    }
    assert(pg_slab_live_count(&s) == 10);

    for (int i = 0; i < 5; i++)
        pg_slab_release(&s, objs[i]);
    assert(pg_slab_live_count(&s) == 5);

    pg_slab_destroy(&s);
    return 0;
}

int test_string_intern(void) {
    pg_string_intern si;
    assert(pg_intern_init(&si, 16) == PG_OK);

    uint32_t id1 = pg_intern_put(&si, "hello", 5);
    uint32_t id2 = pg_intern_put(&si, "world", 5);
    uint32_t id3 = pg_intern_put(&si, "hello", 5);

    assert(id1 != 0 && id2 != 0);
    assert(id1 == id3);
    assert(id1 != id2);

    const char *s = pg_intern_get(&si, id1);
    assert(s && strcmp(s, "hello") == 0);

    assert(pg_intern_find(&si, "world", 5) == id2);
    assert(pg_intern_find(&si, "missing", 7) == 0);
    assert(pg_intern_contains(&si, "hello", 5));

    pg_intern_free(&si);
    return 0;
}
