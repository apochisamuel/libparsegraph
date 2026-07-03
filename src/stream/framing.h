#ifndef PG_FRAMING_H
#define PG_FRAMING_H

#include "parsegraph/types.h"
#include "../util/buffer.h"

typedef struct pg_frame {
    uint8_t  opcode;
    uint16_t payload_len;
    uint8_t *payload;         /* BUG-10 ALIAS: raw pointer into buffer */
} pg_frame;

typedef struct pg_frame_decoder {
    pg_buffer *recv_buf;       /* NOT owned — points to protocol's buffer */
    uint32_t   decode_offset;
    bool       header_read;
    pg_frame   current;
} pg_frame_decoder;

pg_status pg_framing_init(pg_frame_decoder *fd, pg_buffer *recv_buf);
void      pg_framing_reset(pg_frame_decoder *fd);
bool      pg_framing_decode_header(pg_frame_decoder *fd);
bool      pg_framing_decode_payload(pg_frame_decoder *fd, pg_frame *out);

#endif
