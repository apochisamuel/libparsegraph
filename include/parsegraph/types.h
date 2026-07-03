#ifndef PARSEGRAPH_TYPES_H
#define PARSEGRAPH_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Identifiers ────────────────────────────────────────────────── */

typedef uint64_t pg_node_id;
typedef uint64_t pg_edge_id;
typedef uint32_t pg_label_id;
typedef uint32_t pg_prop_key;
typedef uint32_t pg_generation;
typedef uint32_t pg_session_id;
typedef uint32_t pg_txn_id;

#define PG_INVALID_NODE  ((pg_node_id)0)
#define PG_INVALID_EDGE  ((pg_edge_id)0)
#define PG_INVALID_LABEL ((pg_label_id)0)

/* ── Property value ─────────────────────────────────────────────── */

typedef enum {
    PG_VAL_NONE = 0,
    PG_VAL_INT,
    PG_VAL_FLOAT,
    PG_VAL_STRING,
    PG_VAL_BOOL,
    PG_VAL_BLOB
} pg_val_type;

typedef struct {
    pg_val_type type;
    union {
        int64_t   i;
        double    f;
        struct {
            char    *data;
            uint32_t len;
        } str;
        bool      b;
        struct {
            uint8_t *data;
            uint32_t len;
        } blob;
    } v;
} pg_value;

/* ── Graph node ─────────────────────────────────────────────────── */

typedef struct pg_adj_list {
    pg_edge_id *edges;
    uint32_t    count;
    uint32_t    capacity;
} pg_adj_list;

typedef struct pg_prop_entry {
    pg_prop_key  key;
    pg_value     value;
} pg_prop_entry;

typedef struct pg_prop_store {
    pg_prop_entry *entries;
    uint32_t       count;
    uint32_t       capacity;
} pg_prop_store;

typedef struct pg_node {
    pg_node_id    id;
    pg_label_id   label;
    pg_adj_list   outgoing;
    pg_adj_list   incoming;
    pg_prop_store properties;
    pg_generation created_gen;
    pg_generation deleted_gen;   /* 0 = alive */
    uint32_t      refcount;
    uint8_t       flags;
} pg_node;

/* ── Graph edge ─────────────────────────────────────────────────── */

typedef struct pg_edge {
    pg_edge_id    id;
    pg_node_id    src;
    pg_node_id    dst;
    pg_label_id   edge_type;
    pg_prop_store properties;
    pg_generation created_gen;
    pg_generation deleted_gen;
    uint32_t      refcount;
    uint8_t       flags;
} pg_edge;

/* ── Error codes ────────────────────────────────────────────────── */

typedef enum {
    PG_OK = 0,
    PG_ERR_NOMEM,
    PG_ERR_NOT_FOUND,
    PG_ERR_EXISTS,
    PG_ERR_INVALID,
    PG_ERR_OVERFLOW,
    PG_ERR_FULL,
    PG_ERR_IO,
    PG_ERR_PROTOCOL,
    PG_ERR_TXN_CONFLICT,
    PG_ERR_SESSION_CLOSED,
    PG_ERR_INTERNAL
} pg_status;

/* ── Forward declarations ───────────────────────────────────────── */

typedef struct pg_graph       pg_graph;
typedef struct pg_cursor      pg_cursor;
typedef struct pg_query_plan  pg_query_plan;
typedef struct pg_executor    pg_executor;
typedef struct pg_memo_cache  pg_memo_cache;
typedef struct pg_session     pg_session;
typedef struct pg_txn         pg_txn;
typedef struct pg_snapshot    pg_snapshot;

/* ── Limits ─────────────────────────────────────────────────────── */

#define PG_MAX_LABEL_LEN       128
#define PG_MAX_PROP_KEY_LEN    64
#define PG_MAX_STRING_VAL_LEN  4096
#define PG_MAX_BLOB_VAL_LEN    8192
#define PG_MAX_ADJ_INIT        8
#define PG_MAX_PROPS_INIT      4
#define PG_SEGMENT_NODE_CAP    256
#define PG_SEGMENT_EDGE_CAP    512
#define PG_COMPACT_THRESHOLD   0.5
#define PG_HASH_LOAD_FACTOR    0.75
#define PG_MEMO_MAX_ENTRIES    1024
#define PG_MAX_SESSIONS        32
#define PG_MAX_BATCH_OPS       64
#define PG_FRAME_MAX_PAYLOAD   65535

#endif /* PARSEGRAPH_TYPES_H */
