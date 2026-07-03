#ifndef PG_PROTOCOL_H
#define PG_PROTOCOL_H

#include "parsegraph/types.h"
#include "session.h"
#include "framing.h"
#include "batch.h"
#include "txn.h"
#include "../util/buffer.h"

#define PG_OP_ADD_NODE      0x01
#define PG_OP_ADD_EDGE      0x02
#define PG_OP_DELETE_NODE   0x03
#define PG_OP_SET_PROPERTY  0x04
#define PG_OP_QUERY         0x05
#define PG_OP_COMPACT       0x06
#define PG_OP_SESSION_OPEN  0x07
#define PG_OP_SESSION_CLOSE 0x08
#define PG_OP_TXN_BEGIN     0x09
#define PG_OP_TXN_COMMIT    0x0A
#define PG_OP_BATCH         0x0B
#define PG_OP_SNAPSHOT      0x0C
#define PG_OP_INDEX_REBUILD 0x0D

typedef struct pg_protocol_ctx {
    pg_graph          *graph;
    pg_memo_cache     *cache;
    pg_buffer          recv_buf;
    pg_frame_decoder   decoder;
    pg_session        *sessions[PG_MAX_SESSIONS];
    pg_txn            *active_txns[PG_MAX_SESSIONS];
    uint32_t           frame_count;
    uint32_t           error_count;
} pg_protocol_ctx;

pg_status pg_protocol_init(pg_protocol_ctx *ctx, pg_graph *g,
                            pg_memo_cache *cache);
void      pg_protocol_free(pg_protocol_ctx *ctx);
pg_status pg_protocol_feed(pg_protocol_ctx *ctx, const uint8_t *data,
                            size_t len);
pg_status pg_protocol_process(pg_protocol_ctx *ctx);
pg_status pg_protocol_handle_frame(pg_protocol_ctx *ctx, pg_frame *frame);

#endif
