#include "parsegraph/graph.h"
#include "../src/stream/protocol.h"
#include "../src/memo/cache.h"
#include <assert.h>

int test_stream(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);
    pg_memo_cache *cache;
    assert(pg_cache_create(&cache, 64) == PG_OK);

    pg_protocol_ctx proto;
    assert(pg_protocol_init(&proto, g, cache) == PG_OK);

    uint8_t frame[] = { 0x01, 0x02, 0x00, 0x05, 0x00 };
    pg_protocol_feed(&proto, frame, sizeof(frame));
    pg_protocol_process(&proto);

    assert(pg_graph_node_count(g) == 1);
    assert(proto.frame_count == 1);

    pg_protocol_free(&proto);
    pg_cache_destroy(cache);
    pg_graph_destroy(g);
    return 0;
}

int test_stream_multi_frame(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);
    pg_memo_cache *cache;
    assert(pg_cache_create(&cache, 64) == PG_OK);

    pg_protocol_ctx proto;
    assert(pg_protocol_init(&proto, g, cache) == PG_OK);

    /* add 3 nodes, then add edges between them */
    uint8_t frames[] = {
        /* ADD_NODE label=1 props=0 */
        0x01, 0x02, 0x00, 0x01, 0x00,
        /* ADD_NODE label=2 props=0 */
        0x01, 0x02, 0x00, 0x02, 0x00,
        /* ADD_NODE label=1 props=0 */
        0x01, 0x02, 0x00, 0x01, 0x00,
        /* ADD_EDGE src=1 dst=2 type=1 */
        0x02, 0x09, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00,
            0x01,
    };
    pg_protocol_feed(&proto, frames, sizeof(frames));
    pg_protocol_process(&proto);

    assert(pg_graph_node_count(g) == 3);
    assert(pg_graph_edge_count(g) == 1);
    assert(proto.frame_count == 4);

    pg_protocol_free(&proto);
    pg_cache_destroy(cache);
    pg_graph_destroy(g);
    return 0;
}

int test_stream_session_lifecycle(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);
    pg_memo_cache *cache;
    assert(pg_cache_create(&cache, 64) == PG_OK);

    pg_protocol_ctx proto;
    assert(pg_protocol_init(&proto, g, cache) == PG_OK);

    /* SESSION_OPEN sid=0 */
    uint8_t open[] = { 0x07, 0x01, 0x00, 0x00 };
    pg_protocol_feed(&proto, open, sizeof(open));
    pg_protocol_process(&proto);
    assert(proto.sessions[0] != NULL);

    /* SESSION_CLOSE sid=0 */
    uint8_t close[] = { 0x08, 0x01, 0x00, 0x00 };
    pg_protocol_feed(&proto, close, sizeof(close));
    pg_protocol_process(&proto);
    assert(proto.sessions[0] == NULL);

    pg_protocol_free(&proto);
    pg_cache_destroy(cache);
    pg_graph_destroy(g);
    return 0;
}
