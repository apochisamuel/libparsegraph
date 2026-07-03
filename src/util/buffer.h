#ifndef PG_BUFFER_H
#define PG_BUFFER_H

#include "parsegraph/types.h"

typedef struct pg_buffer {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} pg_buffer;

pg_status pg_buffer_init(pg_buffer *buf, size_t initial_cap);
void      pg_buffer_free(pg_buffer *buf);
pg_status pg_buffer_append(pg_buffer *buf, const uint8_t *src, size_t n);
pg_status pg_buffer_ensure(pg_buffer *buf, size_t needed);
void      pg_buffer_consume(pg_buffer *buf, size_t n);
pg_status pg_buffer_write_u8(pg_buffer *buf, uint8_t val);
pg_status pg_buffer_write_u16(pg_buffer *buf, uint16_t val);
pg_status pg_buffer_write_u32(pg_buffer *buf, uint32_t val);
pg_status pg_buffer_write_u64(pg_buffer *buf, uint64_t val);
bool      pg_buffer_read_u8(const pg_buffer *buf, size_t off, uint8_t *out);
bool      pg_buffer_read_u16(const pg_buffer *buf, size_t off, uint16_t *out);
bool      pg_buffer_read_u32(const pg_buffer *buf, size_t off, uint32_t *out);
void      pg_buffer_reset(pg_buffer *buf);
uint8_t  *pg_buffer_tail(pg_buffer *buf);
size_t    pg_buffer_remaining(const pg_buffer *buf);

#endif /* PG_BUFFER_H */
