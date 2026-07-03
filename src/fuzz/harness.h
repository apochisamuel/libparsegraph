#ifndef PG_HARNESS_H
#define PG_HARNESS_H

#include "parsegraph/types.h"
#include "parsegraph/graph.h"
#include "../stream/protocol.h"
#include "../memo/cache.h"
#include "../core/graph_internal.h"
#include "../core/properties.h"
#include "../stream/session.h"
#include "../stream/txn.h"
#include "../storage/compact.h"
#include "../storage/segment.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Byte reader utilities ───────────────────────────────── */

typedef struct pg_fuzz_reader {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
} pg_fuzz_reader;

static inline void fuzz_reader_init(pg_fuzz_reader *r,
                                     const uint8_t *data, size_t len) {
    r->data = data;
    r->len = len;
    r->pos = 0;
}

static inline bool fuzz_reader_done(const pg_fuzz_reader *r) {
    return r->pos >= r->len;
}

static inline size_t fuzz_reader_remaining(const pg_fuzz_reader *r) {
    return r->pos < r->len ? r->len - r->pos : 0;
}

static inline uint8_t fuzz_read_u8(pg_fuzz_reader *r) {
    if (r->pos >= r->len) return 0;
    return r->data[r->pos++];
}

static inline uint16_t fuzz_read_u16(pg_fuzz_reader *r) {
    if (r->pos + 2 > r->len) return 0;
    uint16_t v = (uint16_t)r->data[r->pos] |
                  ((uint16_t)r->data[r->pos + 1] << 8);
    r->pos += 2;
    return v;
}

static inline uint32_t fuzz_read_u32(pg_fuzz_reader *r) {
    if (r->pos + 4 > r->len) return 0;
    uint32_t v;
    memcpy(&v, r->data + r->pos, 4);
    r->pos += 4;
    return v;
}

static inline int64_t fuzz_read_i64(pg_fuzz_reader *r) {
    if (r->pos + 8 > r->len) return 0;
    int64_t v;
    memcpy(&v, r->data + r->pos, 8);
    r->pos += 8;
    return v;
}

static inline const uint8_t *fuzz_read_bytes(pg_fuzz_reader *r,
                                               size_t n) {
    if (r->pos + n > r->len) return NULL;
    const uint8_t *p = r->data + r->pos;
    r->pos += n;
    return p;
}

/* ── Harness context ─────────────────────────────────────── */

typedef struct pg_harness {
    pg_graph          *graph;
    pg_memo_cache     *cache;
    pg_protocol_ctx    proto;
    bool               initialized;
} pg_harness;

/*
 * Initialize harness with a fresh graph and cache.
 */
static inline pg_status pg_harness_init(pg_harness *h) {
    memset(h, 0, sizeof(*h));

    if (pg_graph_create(&h->graph) != PG_OK) return PG_ERR_NOMEM;
    if (pg_cache_create(&h->cache, PG_MEMO_MAX_ENTRIES) != PG_OK) {
        pg_graph_destroy(h->graph);
        return PG_ERR_NOMEM;
    }
    if (pg_protocol_init(&h->proto, h->graph, h->cache) != PG_OK) {
        pg_cache_destroy(h->cache);
        pg_graph_destroy(h->graph);
        return PG_ERR_NOMEM;
    }

    h->initialized = true;
    return PG_OK;
}

/*
 * Tear down harness.
 */
static inline void pg_harness_free(pg_harness *h) {
    if (!h->initialized) return;
    pg_protocol_free(&h->proto);
    pg_cache_destroy(h->cache);
    pg_graph_destroy(h->graph);
    h->initialized = false;
}

/*
 * Seed the harness graph with baseline topology.
 * Creates a connected graph with multiple labels, edge types,
 * and properties so the fuzzer can immediately exercise deep
 * code paths without needing to build up state from scratch.
 */
static inline void pg_harness_seed(pg_harness *h) {
    pg_graph *g = h->graph;

    /* intern labels */
    pg_label_id lbl_person  = pg_graph_intern_label(g, "Person",  6);
    pg_label_id lbl_city    = pg_graph_intern_label(g, "City",    4);
    pg_label_id lbl_company = pg_graph_intern_label(g, "Company", 7);
    pg_label_id lbl_product = pg_graph_intern_label(g, "Product", 7);

    pg_label_id et_knows    = pg_graph_intern_label(g, "KNOWS",    5);
    pg_label_id et_lives    = pg_graph_intern_label(g, "LIVES_IN", 8);
    pg_label_id et_works    = pg_graph_intern_label(g, "WORKS_AT", 8);
    pg_label_id et_makes    = pg_graph_intern_label(g, "MAKES",    5);

    /* create nodes: 4 people, 2 cities, 2 companies, 2 products */
    pg_node_id people[4], cities[2], companies[2], products[2];
    for (int i = 0; i < 4; i++) {
        people[i] = pg_graph_add_node(g, lbl_person);
        pg_value age = { .type = PG_VAL_INT, .v.i = 25 + i * 5 };
        pg_graph_set_node_prop(g, people[i], 0, &age);
        pg_value score = { .type = PG_VAL_INT, .v.i = (i + 1) * 100 };
        pg_graph_set_node_prop(g, people[i], 1, &score);
    }
    for (int i = 0; i < 2; i++) {
        cities[i] = pg_graph_add_node(g, lbl_city);
        pg_value pop = { .type = PG_VAL_INT, .v.i = (i + 1) * 500000 };
        pg_graph_set_node_prop(g, cities[i], 0, &pop);
    }
    for (int i = 0; i < 2; i++) {
        companies[i] = pg_graph_add_node(g, lbl_company);
        pg_value rev = { .type = PG_VAL_INT, .v.i = (i + 1) * 1000000 };
        pg_graph_set_node_prop(g, companies[i], 0, &rev);
    }
    for (int i = 0; i < 2; i++) {
        products[i] = pg_graph_add_node(g, lbl_product);
        pg_value price = { .type = PG_VAL_INT, .v.i = (i + 1) * 50 };
        pg_graph_set_node_prop(g, products[i], 0, &price);
    }

    /* create edges: social graph + work + products */
    pg_graph_add_edge(g, people[0], people[1], et_knows);
    pg_graph_add_edge(g, people[1], people[2], et_knows);
    pg_graph_add_edge(g, people[2], people[3], et_knows);
    pg_graph_add_edge(g, people[0], people[3], et_knows);
    pg_graph_add_edge(g, people[3], people[1], et_knows);

    pg_graph_add_edge(g, people[0], cities[0], et_lives);
    pg_graph_add_edge(g, people[1], cities[0], et_lives);
    pg_graph_add_edge(g, people[2], cities[1], et_lives);
    pg_graph_add_edge(g, people[3], cities[1], et_lives);

    pg_graph_add_edge(g, people[0], companies[0], et_works);
    pg_graph_add_edge(g, people[1], companies[0], et_works);
    pg_graph_add_edge(g, people[2], companies[1], et_works);
    pg_graph_add_edge(g, people[3], companies[1], et_works);

    pg_graph_add_edge(g, companies[0], products[0], et_makes);
    pg_graph_add_edge(g, companies[1], products[1], et_makes);

    /* open a default session so queries work immediately */
    pg_session_create(&h->proto.sessions[0], 0, g, h->cache);
}

/*
 * Feed raw bytes through the protocol and process all frames.
 */
static inline void pg_harness_feed(pg_harness *h,
                                     const uint8_t *data, size_t len) {
    pg_protocol_feed(&h->proto, data, len);
    pg_protocol_process(&h->proto);
}

/*
 * Build a frame manually and feed it.
 */
static inline void pg_harness_inject_frame(pg_harness *h, uint8_t opcode,
                                             const uint8_t *payload,
                                             uint16_t payload_len) {
    uint8_t header[3];
    header[0] = opcode;
    header[1] = (uint8_t)(payload_len & 0xFF);
    header[2] = (uint8_t)((payload_len >> 8) & 0xFF);
    pg_protocol_feed(&h->proto, header, 3);
    if (payload_len > 0 && payload) {
        pg_protocol_feed(&h->proto, payload, payload_len);
    }
    pg_protocol_process(&h->proto);
}

/*
 * Inject an ADD_NODE frame.
 */
static inline pg_node_id pg_harness_add_node(pg_harness *h,
                                               pg_label_id label) {
    uint32_t before = pg_graph_node_count(h->graph);
    uint8_t payload[2] = { (uint8_t)label, 0x00 };
    pg_harness_inject_frame(h, 0x01, payload, 2);
    uint32_t after = pg_graph_node_count(h->graph);
    return (after > before) ? (pg_node_id)after : PG_INVALID_NODE;
}

/*
 * Inject an ADD_EDGE frame.
 */
static inline void pg_harness_add_edge(pg_harness *h, pg_node_id src,
                                         pg_node_id dst,
                                         pg_label_id etype) {
    uint8_t payload[9];
    memcpy(payload, &src, 4);
    memcpy(payload + 4, &dst, 4);
    payload[8] = (uint8_t)etype;
    pg_harness_inject_frame(h, 0x02, payload, 9);
}

/*
 * Inject a DELETE_NODE frame.
 */
static inline void pg_harness_delete_node(pg_harness *h, pg_node_id id) {
    uint8_t payload[4];
    memcpy(payload, &id, 4);
    pg_harness_inject_frame(h, 0x03, payload, 4);
}

/*
 * Inject a SET_PROPERTY frame.
 */
static inline void pg_harness_set_prop(pg_harness *h, pg_node_id id,
                                         pg_prop_key key, int64_t val) {
    uint8_t payload[13];
    memcpy(payload, &id, 4);
    payload[4] = (uint8_t)key;
    memcpy(payload + 5, &val, 8);
    pg_harness_inject_frame(h, 0x04, payload, 13);
}

/*
 * Inject a QUERY frame with a simple pattern match.
 */
static inline void pg_harness_query(pg_harness *h, pg_label_id label) {
    uint8_t payload[] = { 0x01, (uint8_t)(label - 1), 0x00, 0x00, 0x00 };
    pg_harness_inject_frame(h, 0x05, payload, sizeof(payload));
}

/*
 * Inject a COMPACT frame.
 */
static inline void pg_harness_compact(pg_harness *h) {
    pg_harness_inject_frame(h, 0x06, NULL, 0);
}

/*
 * Inject a SESSION_OPEN frame.
 */
static inline void pg_harness_session_open(pg_harness *h, uint8_t sid) {
    pg_harness_inject_frame(h, 0x07, &sid, 1);
}

/*
 * Inject a SESSION_CLOSE frame.
 */
static inline void pg_harness_session_close(pg_harness *h, uint8_t sid) {
    pg_harness_inject_frame(h, 0x08, &sid, 1);
}

/*
 * Inject TXN_BEGIN frame.
 */
static inline void pg_harness_txn_begin(pg_harness *h, uint8_t sid) {
    pg_harness_inject_frame(h, 0x09, &sid, 1);
}

/*
 * Inject TXN_COMMIT frame.
 */
static inline void pg_harness_txn_commit(pg_harness *h, uint8_t sid) {
    pg_harness_inject_frame(h, 0x0A, &sid, 1);
}

/*
 * Inject a BATCH frame with sub-operations.
 */
static inline void pg_harness_batch(pg_harness *h, uint8_t sid,
                                      const uint8_t *ops, uint16_t ops_len) {
    uint8_t header[2] = { sid, (uint8_t)(ops_len > 0 ? ops_len : 1) };
    uint16_t total = 2 + ops_len;
    uint8_t *payload = malloc(total);
    if (!payload) return;
    memcpy(payload, header, 2);
    if (ops_len > 0) memcpy(payload + 2, ops, ops_len);
    pg_harness_inject_frame(h, 0x0B, payload, total);
    free(payload);
}

/*
 * Inject a SNAPSHOT frame.
 */
static inline void pg_harness_snapshot(pg_harness *h) {
    pg_harness_inject_frame(h, 0x0C, NULL, 0);
}

#endif /* PG_HARNESS_H */
