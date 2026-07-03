#ifndef PG_BATCH_H
#define PG_BATCH_H

#include "parsegraph/types.h"
#include "session.h"

typedef enum {
    PG_BATCH_ADD_NODE,
    PG_BATCH_ADD_EDGE,
    PG_BATCH_DEL_NODE,
    PG_BATCH_SET_PROP,
    PG_BATCH_QUERY,
    PG_BATCH_COMPACT
} pg_batch_op_type;

typedef struct pg_batch_op {
    pg_batch_op_type type;
    union {
        struct { pg_label_id label; } add_node;
        struct { pg_node_id src, dst; pg_label_id type; } add_edge;
        struct { pg_node_id id; } del_node;
        struct { pg_node_id id; pg_prop_key key; pg_value val; } set_prop;
        struct { const uint8_t *data; uint32_t len; } query;
    } data;
} pg_batch_op;

typedef struct pg_batch_ctx {
    pg_session   *session;
    pg_batch_op  *ops;
    uint32_t      op_count;
    uint32_t      op_capacity;
    uint32_t      executed;
} pg_batch_ctx;

pg_status pg_batch_init(pg_batch_ctx *bc, pg_session *sess);
void      pg_batch_free(pg_batch_ctx *bc);
pg_status pg_batch_add_op(pg_batch_ctx *bc, const pg_batch_op *op);
pg_status pg_batch_execute(pg_batch_ctx *bc);

#endif
