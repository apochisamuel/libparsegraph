#include "framing.h"
#include <string.h>

pg_status pg_framing_init(pg_frame_decoder *fd, pg_buffer *recv_buf) {
    fd->recv_buf = recv_buf;
    fd->decode_offset = 0;
    fd->header_read = false;
    memset(&fd->current, 0, sizeof(pg_frame));
    return PG_OK;
}

void pg_framing_reset(pg_frame_decoder *fd) {
    fd->decode_offset = 0;
    fd->header_read = false;
    memset(&fd->current, 0, sizeof(pg_frame));
}

/*
 * BUG-10 ALIAS SITE:
 * Stores a raw pointer into the receive buffer's data array.
 * If protocol_feed() later appends data that triggers buffer_ensure
 * (realloc), this pointer becomes dangling.
 *
 * The missing abstraction: BufferHandle type using offset + length
 * instead of raw pointers, resolved just before access.
 */
bool pg_framing_decode_header(pg_frame_decoder *fd) {
    if (fd->header_read) return true;
    if (fd->recv_buf->len < fd->decode_offset + 3) return false;

    uint8_t *base = fd->recv_buf->data + fd->decode_offset;
    fd->current.opcode = base[0];
    fd->current.payload_len = (uint16_t)base[1] |
                               ((uint16_t)base[2] << 8);

    /* cap payload to prevent oversized reads */
    if (fd->current.payload_len > PG_FRAME_MAX_PAYLOAD)
        fd->current.payload_len = PG_FRAME_MAX_PAYLOAD;

    fd->decode_offset += 3;
    fd->header_read = true;

    /* pre-compute payload pointer — BUG: this caches into a
       potentially-movable buffer */
    fd->current.payload = fd->recv_buf->data + fd->decode_offset;

    return true;
}

/*
 * BUG-10 CRASH SITE:
 * Reads through current.payload which was set during decode_header.
 * If the buffer has been reallocated since then (protocol_feed
 * appended more data), this reads freed/invalid memory.
 */
bool pg_framing_decode_payload(pg_frame_decoder *fd, pg_frame *out) {
    if (!fd->header_read) return false;
    if (fd->recv_buf->len < fd->decode_offset + fd->current.payload_len)
        return false;

    out->opcode = fd->current.opcode;
    out->payload_len = fd->current.payload_len;
    out->payload = fd->current.payload;        /* ← crash: stale pointer */

    fd->decode_offset += fd->current.payload_len;
    fd->header_read = false;
    return true;
}

/* ── Extended framing operations ─────────────────────────── */

/*
 * Check if there's enough data in the buffer for a complete frame.
 */
bool pg_framing_has_complete_frame(pg_frame_decoder *fd) {
    if (fd->recv_buf->len < fd->decode_offset + 3)
        return false;

    uint8_t *base = fd->recv_buf->data + fd->decode_offset;
    uint16_t plen = (uint16_t)base[1] | ((uint16_t)base[2] << 8);

    return fd->recv_buf->len >= fd->decode_offset + 3 + plen;
}

/*
 * Peek at the next frame's opcode without advancing the decoder.
 */
bool pg_framing_peek_opcode(pg_frame_decoder *fd, uint8_t *out) {
    if (fd->recv_buf->len < fd->decode_offset + 1)
        return false;
    *out = fd->recv_buf->data[fd->decode_offset];
    return true;
}

/*
 * Skip the current frame without processing it.
 */
bool pg_framing_skip(pg_frame_decoder *fd) {
    if (fd->recv_buf->len < fd->decode_offset + 3)
        return false;

    uint8_t *base = fd->recv_buf->data + fd->decode_offset;
    uint16_t plen = (uint16_t)base[1] | ((uint16_t)base[2] << 8);

    if (fd->recv_buf->len < fd->decode_offset + 3 + plen)
        return false;

    fd->decode_offset += 3 + plen;
    fd->header_read = false;
    return true;
}

/*
 * Count the number of complete frames available in the buffer.
 */
uint32_t pg_framing_count_frames(pg_frame_decoder *fd) {
    uint32_t count = 0;
    uint32_t pos = fd->decode_offset;

    while (pos + 3 <= fd->recv_buf->len) {
        uint16_t plen = (uint16_t)fd->recv_buf->data[pos + 1] |
                         ((uint16_t)fd->recv_buf->data[pos + 2] << 8);
        if (pos + 3 + plen > fd->recv_buf->len) break;
        count++;
        pos += 3 + plen;
    }
    return count;
}

/*
 * Encode a frame into a destination buffer.
 */
pg_status pg_framing_encode(pg_buffer *dst, uint8_t opcode,
                             const uint8_t *payload, uint16_t payload_len) {
    uint8_t header[3];
    header[0] = opcode;
    header[1] = (uint8_t)(payload_len & 0xFF);
    header[2] = (uint8_t)((payload_len >> 8) & 0xFF);

    pg_status s = pg_buffer_append(dst, header, 3);
    if (s != PG_OK) return s;

    if (payload_len > 0 && payload) {
        return pg_buffer_append(dst, payload, payload_len);
    }
    return PG_OK;
}
