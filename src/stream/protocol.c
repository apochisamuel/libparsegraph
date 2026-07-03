#include "protocol.h"
#include "parsegraph/graph.h"
#include "../storage/compact.h"
#include "../storage/segment.h"
#include "../index/label_index.h"
#include "../index/hash.h"
#include "../query/parser.h"
#include "../query/planner.h"
#include "../query/optimizer.h"
#include "../core/properties.h"
#include "../util/error.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_protocol_init(pg_protocol_ctx *ctx, pg_graph *g,
                            pg_memo_cache *cache) {
    ctx->graph = g;
    ctx->cache = cache;
    pg_status s = pg_buffer_init(&ctx->recv_buf, 4096);
    if (s != PG_OK) return s;
    pg_framing_init(&ctx->decoder, &ctx->recv_buf);
    memset(ctx->sessions, 0, sizeof(ctx->sessions));
    memset(ctx->active_txns, 0, sizeof(ctx->active_txns));
    ctx->frame_count = 0;
    ctx->error_count = 0;
    return PG_OK;
}

void pg_protocol_free(pg_protocol_ctx *ctx) {
    for (uint32_t i = 0; i < PG_MAX_SESSIONS; i++) {
        if (ctx->sessions[i]) pg_session_cleanup(ctx->sessions[i]);
        if (ctx->active_txns[i]) pg_txn_abort(ctx->active_txns[i]);
    }
    pg_buffer_free(&ctx->recv_buf);
}

/*
 * BUG-10 THROUGH:
 * Feeding data can trigger buffer realloc, invalidating any
 * cached pointers in the frame decoder.
 */
pg_status pg_protocol_feed(pg_protocol_ctx *ctx, const uint8_t *data,
                            size_t len) {
    return pg_buffer_append(&ctx->recv_buf, data, len);
}

static uint8_t read_u8(uint8_t *p, uint32_t *off, uint32_t len) {
    if (*off >= len) return 0;
    return p[(*off)++];
}

static uint16_t read_u16(uint8_t *p, uint32_t *off, uint32_t len) {
    if (*off + 2 > len) return 0;
    uint16_t v = (uint16_t)p[*off] | ((uint16_t)p[*off + 1] << 8);
    *off += 2;
    return v;
}

static uint32_t read_u32(uint8_t *p, uint32_t *off, uint32_t len) {
    if (*off + 4 > len) return 0;
    uint32_t v;
    memcpy(&v, p + *off, 4);
    *off += 4;
    return v;
}

static int64_t read_i64(uint8_t *p, uint32_t *off, uint32_t len) {
    if (*off + 8 > len) return 0;
    int64_t v;
    memcpy(&v, p + *off, 8);
    *off += 8;
    return v;
}

pg_status pg_protocol_handle_frame(pg_protocol_ctx *ctx, pg_frame *frame) {
    uint8_t *p = frame->payload;
    uint32_t plen = frame->payload_len;
    uint32_t off = 0;

    switch (frame->opcode) {

    case PG_OP_ADD_NODE: {
        uint8_t label_raw = read_u8(p, &off, plen);
        pg_label_id label = (pg_label_id)(label_raw % 16) + 1;
        pg_node_id nid = pg_graph_add_node(ctx->graph, label);

        /* set properties from remaining payload */
        uint8_t prop_count = read_u8(p, &off, plen);
        prop_count = prop_count % 4;
        for (uint8_t i = 0; i < prop_count && off + 9 <= plen; i++) {
            pg_prop_key key = (pg_prop_key)read_u8(p, &off, plen);
            pg_value val = { .type = PG_VAL_INT };
            val.v.i = read_i64(p, &off, plen);
            pg_graph_set_node_prop(ctx->graph, nid, key, &val);
        }
        break;
    }

    case PG_OP_ADD_EDGE: {
        uint32_t src = read_u32(p, &off, plen);
        uint32_t dst = read_u32(p, &off, plen);
        uint8_t type_raw = read_u8(p, &off, plen);
        pg_label_id etype = (pg_label_id)(type_raw % 8) + 1;
        pg_graph_add_edge(ctx->graph, (pg_node_id)src,
                          (pg_node_id)dst, etype);
        break;
    }

    case PG_OP_DELETE_NODE: {
        uint32_t nid = read_u32(p, &off, plen);
        pg_graph_remove_node(ctx->graph, (pg_node_id)nid);
        break;
    }

    case PG_OP_SET_PROPERTY: {
        uint32_t nid = read_u32(p, &off, plen);
        uint8_t key_raw = read_u8(p, &off, plen);
        pg_value val = { .type = PG_VAL_INT };
        val.v.i = read_i64(p, &off, plen);
        pg_graph_set_node_prop(ctx->graph, (pg_node_id)nid,
                               (pg_prop_key)key_raw, &val);
        break;
    }

    case PG_OP_QUERY: {
        /* find or create default session */
        if (!ctx->sessions[0]) {
            pg_session_create(&ctx->sessions[0], 0, ctx->graph, ctx->cache);
        }
        pg_session *sess = ctx->sessions[0];
        if (!sess || !pg_session_is_open(sess)) break;

        pg_parse_ctx pctx = {
            .input = p + off,
            .len = plen - off,
            .pos = 0,
            .intern_label = NULL,
            .intern_ctx = NULL
        };
        pg_ast_node *ast = NULL;
        if (pg_parse_query(&pctx, &ast) == PG_OK && ast) {
            pg_query_plan *plan = NULL;
            if (pg_planner_create_plan(ast, &plan) == PG_OK) {
                pg_optimizer opt;
                pg_optimizer_init(&opt, plan);
                pg_optimizer_run(&opt);

                pg_session_execute_query(sess, plan);
                pg_planner_free_plan(plan);
            }
            pg_ast_free(ast);
        }
        break;
    }

    case PG_OP_COMPACT: {
        pg_segment_mgr nsm, esm;
        pg_segment_mgr_init(&nsm, pg_graph_node_arena(ctx->graph));
        pg_segment_mgr_init(&esm, pg_graph_edge_arena(ctx->graph));

        pg_compact_ctx cctx;
        pg_compact_init(&cctx, ctx->graph, &nsm, &esm);
        pg_compact_execute(&cctx);

        pg_segment_mgr_free(&nsm);
        pg_segment_mgr_free(&esm);
        break;
    }

    case PG_OP_SESSION_OPEN: {
        uint8_t sid = read_u8(p, &off, plen);
        sid = sid % PG_MAX_SESSIONS;
        if (!ctx->sessions[sid]) {
            pg_session_create(&ctx->sessions[sid], sid,
                              ctx->graph, ctx->cache);
        }
        break;
    }

    case PG_OP_SESSION_CLOSE: {
        uint8_t sid = read_u8(p, &off, plen);
        sid = sid % PG_MAX_SESSIONS;
        /*
         * BUG-7 TRIGGER:
         * Session close doesn't check for active transactions.
         * If active_txns[sid] is non-NULL, the txn holds a stale
         * pointer after cleanup.
         */
        if (ctx->sessions[sid]) {
            pg_session_cleanup(ctx->sessions[sid]);
            ctx->sessions[sid] = NULL;
        }
        break;
    }

    case PG_OP_TXN_BEGIN: {
        uint8_t sid = read_u8(p, &off, plen);
        sid = sid % PG_MAX_SESSIONS;
        if (ctx->sessions[sid] && !ctx->active_txns[sid]) {
            pg_txn_begin(&ctx->active_txns[sid], ctx->frame_count,
                         ctx->sessions[sid]);
        }
        break;
    }

    case PG_OP_TXN_COMMIT: {
        uint8_t sid = read_u8(p, &off, plen);
        sid = sid % PG_MAX_SESSIONS;
        if (ctx->active_txns[sid]) {
            pg_txn_commit(ctx->active_txns[sid]);
            ctx->active_txns[sid] = NULL;
        }
        break;
    }

    case PG_OP_BATCH: {
        uint8_t sid = read_u8(p, &off, plen);
        sid = sid % PG_MAX_SESSIONS;
        if (!ctx->sessions[sid]) {
            pg_session_create(&ctx->sessions[sid], sid,
                              ctx->graph, ctx->cache);
        }
        pg_session *sess = ctx->sessions[sid];
        if (!sess) break;

        pg_batch_ctx bctx;
        pg_batch_init(&bctx, sess);

        uint8_t op_count = read_u8(p, &off, plen);
        op_count = (op_count % 8) + 1;

        for (uint8_t i = 0; i < op_count && off < plen; i++) {
            uint8_t sub_op = read_u8(p, &off, plen);
            pg_batch_op bop;
            memset(&bop, 0, sizeof(bop));

            switch (sub_op % 5) {
            case 0:
                bop.type = PG_BATCH_ADD_NODE;
                bop.data.add_node.label = (pg_label_id)(read_u8(p, &off, plen) % 16) + 1;
                break;
            case 1:
                bop.type = PG_BATCH_ADD_EDGE;
                bop.data.add_edge.src = (pg_node_id)read_u32(p, &off, plen);
                bop.data.add_edge.dst = (pg_node_id)read_u32(p, &off, plen);
                bop.data.add_edge.type = (pg_label_id)(read_u8(p, &off, plen) % 8) + 1;
                break;
            case 2:
                bop.type = PG_BATCH_DEL_NODE;
                bop.data.del_node.id = (pg_node_id)read_u32(p, &off, plen);
                break;
            case 3:
                bop.type = PG_BATCH_SET_PROP;
                bop.data.set_prop.id = (pg_node_id)read_u32(p, &off, plen);
                bop.data.set_prop.key = (pg_prop_key)read_u8(p, &off, plen);
                bop.data.set_prop.val.type = PG_VAL_INT;
                bop.data.set_prop.val.v.i = read_i64(p, &off, plen);
                break;
            case 4:
                bop.type = PG_BATCH_QUERY;
                bop.data.query.data = p + off;
                bop.data.query.len = (plen - off > 32) ? 32 : plen - off;
                off += bop.data.query.len;
                break;
            }
            pg_batch_add_op(&bctx, &bop);
        }

        pg_batch_execute(&bctx);
        pg_batch_free(&bctx);
        break;
    }

    case PG_OP_SNAPSHOT: {
        /* just exercise the snapshot path */
        pg_snapshot *snap = NULL;
        pg_graph_snapshot(ctx->graph, &snap);
        if (snap) pg_snapshot_release(snap);
        break;
    }

    case PG_OP_INDEX_REBUILD: {
        /* exercise hash grow path for Bug 6 */
        /* not fully wired since we'd need a persistent index;
           the label_index is exercised through batch queries */
        break;
    }

    default:
        ctx->error_count++;
        break;
    }

    ctx->frame_count++;
    return PG_OK;
}

pg_status pg_protocol_process(pg_protocol_ctx *ctx) {
    while (true) {
        if (!pg_framing_decode_header(&ctx->decoder)) break;

        pg_frame frame;
        if (!pg_framing_decode_payload(&ctx->decoder, &frame)) break;

        pg_protocol_handle_frame(ctx, &frame);
    }

    /* consume processed bytes */
    if (ctx->decoder.decode_offset > 0) {
        pg_buffer_consume(&ctx->recv_buf, ctx->decoder.decode_offset);
        ctx->decoder.decode_offset = 0;
    }
    return PG_OK;
}

/* ── Extended protocol handling ──────────────────────────── */

/*
 * Validate a frame before processing.
 * Returns PG_OK if the frame is well-formed, error otherwise.
 */
static pg_status validate_frame(pg_frame *frame) {
    if (frame->opcode < 0x01 || frame->opcode > 0x0D)
        return PG_ERR_PROTOCOL;
    if (frame->payload_len > PG_FRAME_MAX_PAYLOAD)
        return PG_ERR_OVERFLOW;

    /* opcode-specific minimum payload sizes */
    switch (frame->opcode) {
    case PG_OP_ADD_NODE:
        if (frame->payload_len < 1) return PG_ERR_PROTOCOL;
        break;
    case PG_OP_ADD_EDGE:
        if (frame->payload_len < 9) return PG_ERR_PROTOCOL;
        break;
    case PG_OP_DELETE_NODE:
        if (frame->payload_len < 4) return PG_ERR_PROTOCOL;
        break;
    case PG_OP_SET_PROPERTY:
        if (frame->payload_len < 5) return PG_ERR_PROTOCOL;
        break;
    case PG_OP_QUERY:
        if (frame->payload_len < 2) return PG_ERR_PROTOCOL;
        break;
    case PG_OP_SESSION_OPEN:
    case PG_OP_SESSION_CLOSE:
    case PG_OP_TXN_BEGIN:
    case PG_OP_TXN_COMMIT:
        if (frame->payload_len < 1) return PG_ERR_PROTOCOL;
        break;
    case PG_OP_BATCH:
        if (frame->payload_len < 2) return PG_ERR_PROTOCOL;
        break;
    default:
        break;
    }
    return PG_OK;
}

/*
 * Protocol error recovery: skip to next valid frame header.
 * Scans the buffer looking for a recognizable opcode byte
 * followed by a plausible length.
 */
static pg_status protocol_recover(pg_protocol_ctx *ctx) {
    pg_buffer *buf = &ctx->recv_buf;
    uint32_t scan_pos = 1;  /* skip current byte */

    while (scan_pos + 3 <= buf->len) {
        uint8_t candidate_op = buf->data[scan_pos];
        if (candidate_op >= 0x01 && candidate_op <= 0x0D) {
            uint16_t candidate_len = (uint16_t)buf->data[scan_pos + 1] |
                                      ((uint16_t)buf->data[scan_pos + 2] << 8);
            if (candidate_len <= PG_FRAME_MAX_PAYLOAD) {
                /* looks like a valid frame header */
                pg_buffer_consume(buf, scan_pos);
                pg_framing_reset(&ctx->decoder);
                return PG_OK;
            }
        }
        scan_pos++;
    }

    /* no valid frame found — discard everything */
    pg_buffer_reset(buf);
    pg_framing_reset(&ctx->decoder);
    return PG_ERR_PROTOCOL;
}

/*
 * Process frames with error recovery.
 * If a frame fails validation, attempt to recover and continue
 * processing subsequent frames.
 */
pg_status pg_protocol_process_robust(pg_protocol_ctx *ctx) {
    uint32_t max_errors = 8;
    uint32_t consecutive_errors = 0;

    while (true) {
        if (!pg_framing_decode_header(&ctx->decoder)) break;

        pg_frame frame;
        if (!pg_framing_decode_payload(&ctx->decoder, &frame)) break;

        pg_status vs = validate_frame(&frame);
        if (vs != PG_OK) {
            ctx->error_count++;
            consecutive_errors++;
            if (consecutive_errors >= max_errors) {
                /* too many errors — stop processing */
                pg_buffer_reset(&ctx->recv_buf);
                pg_framing_reset(&ctx->decoder);
                break;
            }
            protocol_recover(ctx);
            continue;
        }

        consecutive_errors = 0;
        pg_protocol_handle_frame(ctx, &frame);
    }

    if (ctx->decoder.decode_offset > 0) {
        pg_buffer_consume(&ctx->recv_buf, ctx->decoder.decode_offset);
        ctx->decoder.decode_offset = 0;
    }
    return PG_OK;
}

/*
 * Protocol statistics.
 */
typedef struct pg_protocol_stats {
    uint32_t frames_processed;
    uint32_t frames_errored;
    uint32_t bytes_consumed;
    uint32_t sessions_active;
    uint32_t txns_active;
    uint32_t graph_nodes;
    uint32_t graph_edges;
} pg_protocol_stats;

void pg_protocol_get_stats(pg_protocol_ctx *ctx, pg_protocol_stats *stats) {
    stats->frames_processed = ctx->frame_count;
    stats->frames_errored = ctx->error_count;
    stats->bytes_consumed = 0;  /* not tracked currently */
    stats->sessions_active = 0;
    stats->txns_active = 0;

    for (uint32_t i = 0; i < PG_MAX_SESSIONS; i++) {
        if (ctx->sessions[i]) stats->sessions_active++;
        if (ctx->active_txns[i]) stats->txns_active++;
    }

    stats->graph_nodes = pg_graph_node_count(ctx->graph);
    stats->graph_edges = pg_graph_edge_count(ctx->graph);
}

/*
 * Multi-frame batch: decode and execute multiple frames in one call.
 * Used by the fuzz target to process the entire input at once.
 * Returns the number of frames successfully processed.
 */
uint32_t pg_protocol_process_batch(pg_protocol_ctx *ctx,
                                    const uint8_t *data, size_t len) {
    uint32_t frames = 0;
    size_t pos = 0;

    while (pos + 3 <= len) {
        uint8_t opcode = data[pos];
        uint16_t plen = (uint16_t)data[pos + 1] |
                         ((uint16_t)data[pos + 2] << 8);

        if (plen > PG_FRAME_MAX_PAYLOAD) plen = PG_FRAME_MAX_PAYLOAD;
        if (pos + 3 + plen > len) break;

        pg_frame frame = {
            .opcode = opcode,
            .payload_len = plen,
            .payload = (uint8_t *)data + pos + 3
        };

        pg_status vs = validate_frame(&frame);
        if (vs == PG_OK) {
            pg_protocol_handle_frame(ctx, &frame);
            frames++;
        } else {
            ctx->error_count++;
        }

        pos += 3 + plen;
    }
    return frames;
}

/*
 * Encode a response frame into a buffer.
 * Used to send results back (not yet used in fuzz mode).
 */
pg_status pg_protocol_encode_frame(pg_buffer *out, uint8_t opcode,
                                    const uint8_t *payload,
                                    uint16_t payload_len) {
    uint8_t header[3];
    header[0] = opcode;
    header[1] = (uint8_t)(payload_len & 0xFF);
    header[2] = (uint8_t)((payload_len >> 8) & 0xFF);

    pg_status s = pg_buffer_append(out, header, 3);
    if (s != PG_OK) return s;

    if (payload_len > 0 && payload) {
        s = pg_buffer_append(out, payload, payload_len);
    }
    return s;
}

/*
 * Encode a simple status response.
 */
pg_status pg_protocol_encode_status(pg_buffer *out, pg_status status) {
    uint8_t payload[1] = { (uint8_t)status };
    return pg_protocol_encode_frame(out, 0xF0, payload, 1);
}

/*
 * Encode an error response with a message.
 */
pg_status pg_protocol_encode_error(pg_buffer *out, pg_status status,
                                    const char *msg, uint32_t msg_len) {
    uint8_t header[5];
    header[0] = (uint8_t)status;
    memcpy(header + 1, &msg_len, 4);

    pg_status s = pg_protocol_encode_frame(out, 0xF1, header, 5);
    if (s != PG_OK) return s;

    if (msg_len > 0 && msg) {
        return pg_buffer_append(out, (const uint8_t *)msg, msg_len);
    }
    return PG_OK;
}
