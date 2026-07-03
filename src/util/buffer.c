#include "buffer.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_buffer_init(pg_buffer *buf, size_t initial_cap) {
    if (initial_cap == 0) initial_cap = 64;
    buf->data = malloc(initial_cap);
    if (!buf->data) return PG_ERR_NOMEM;
    buf->len = 0;
    buf->cap = initial_cap;
    return PG_OK;
}

void pg_buffer_free(pg_buffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

pg_status pg_buffer_ensure(pg_buffer *buf, size_t needed) {
    if (buf->cap >= needed) return PG_OK;
    size_t new_cap = buf->cap;
    while (new_cap < needed) new_cap *= 2;
    uint8_t *new_data = realloc(buf->data, new_cap);
    if (!new_data) return PG_ERR_NOMEM;
    buf->data = new_data;
    buf->cap = new_cap;
    return PG_OK;
}

pg_status pg_buffer_append(pg_buffer *buf, const uint8_t *src, size_t n) {
    pg_status s = pg_buffer_ensure(buf, buf->len + n);
    if (s != PG_OK) return s;
    memcpy(buf->data + buf->len, src, n);
    buf->len += n;
    return PG_OK;
}

void pg_buffer_consume(pg_buffer *buf, size_t n) {
    if (n >= buf->len) {
        buf->len = 0;
        return;
    }
    memmove(buf->data, buf->data + n, buf->len - n);
    buf->len -= n;
}

void pg_buffer_reset(pg_buffer *buf) {
    buf->len = 0;
}

uint8_t *pg_buffer_tail(pg_buffer *buf) {
    return buf->data + buf->len;
}

size_t pg_buffer_remaining(const pg_buffer *buf) {
    return buf->cap - buf->len;
}

/* ── Extended buffer operations ──────────────────────────── */

/*
 * Insert data at a specific position, shifting existing data.
 */
pg_status pg_buffer_insert(pg_buffer *buf, size_t pos,
                            const uint8_t *src, size_t n) {
    if (pos > buf->len) return PG_ERR_INVALID;
    pg_status s = pg_buffer_ensure(buf, buf->len + n);
    if (s != PG_OK) return s;

    /* shift existing data right */
    if (pos < buf->len) {
        memmove(buf->data + pos + n, buf->data + pos, buf->len - pos);
    }
    memcpy(buf->data + pos, src, n);
    buf->len += n;
    return PG_OK;
}

/*
 * Remove data at a specific position.
 */
pg_status pg_buffer_remove(pg_buffer *buf, size_t pos, size_t n) {
    if (pos + n > buf->len) return PG_ERR_INVALID;
    if (pos + n < buf->len) {
        memmove(buf->data + pos, buf->data + pos + n, buf->len - pos - n);
    }
    buf->len -= n;
    return PG_OK;
}

/*
 * Write a uint8 to the buffer.
 */
pg_status pg_buffer_write_u8(pg_buffer *buf, uint8_t val) {
    return pg_buffer_append(buf, &val, 1);
}

/*
 * Write a uint16 (little-endian) to the buffer.
 */
pg_status pg_buffer_write_u16(pg_buffer *buf, uint16_t val) {
    uint8_t bytes[2] = { (uint8_t)(val & 0xFF), (uint8_t)(val >> 8) };
    return pg_buffer_append(buf, bytes, 2);
}

/*
 * Write a uint32 (little-endian) to the buffer.
 */
pg_status pg_buffer_write_u32(pg_buffer *buf, uint32_t val) {
    uint8_t bytes[4];
    memcpy(bytes, &val, 4);
    return pg_buffer_append(buf, bytes, 4);
}

/*
 * Write a uint64 (little-endian) to the buffer.
 */
pg_status pg_buffer_write_u64(pg_buffer *buf, uint64_t val) {
    uint8_t bytes[8];
    memcpy(bytes, &val, 8);
    return pg_buffer_append(buf, bytes, 8);
}

/*
 * Read a uint8 from the buffer at the given offset.
 */
bool pg_buffer_read_u8(const pg_buffer *buf, size_t off, uint8_t *out) {
    if (off >= buf->len) return false;
    *out = buf->data[off];
    return true;
}

/*
 * Read a uint16 (little-endian) from the buffer.
 */
bool pg_buffer_read_u16(const pg_buffer *buf, size_t off, uint16_t *out) {
    if (off + 2 > buf->len) return false;
    *out = (uint16_t)buf->data[off] | ((uint16_t)buf->data[off + 1] << 8);
    return true;
}

/*
 * Read a uint32 (little-endian) from the buffer.
 */
bool pg_buffer_read_u32(const pg_buffer *buf, size_t off, uint32_t *out) {
    if (off + 4 > buf->len) return false;
    memcpy(out, buf->data + off, 4);
    return true;
}

/*
 * Compare buffer contents with a byte array.
 */
bool pg_buffer_equals(const pg_buffer *buf, const uint8_t *data, size_t len) {
    if (buf->len != len) return false;
    return memcmp(buf->data, data, len) == 0;
}

/*
 * Duplicate a buffer.
 */
pg_status pg_buffer_dup(const pg_buffer *src, pg_buffer *dst) {
    pg_status s = pg_buffer_init(dst, src->cap);
    if (s != PG_OK) return s;
    return pg_buffer_append(dst, src->data, src->len);
}

/*
 * Compact buffer: if used less than 25% of capacity, shrink.
 */
pg_status pg_buffer_compact(pg_buffer *buf) {
    if (buf->len == 0) return PG_OK;
    if (buf->len * 4 >= buf->cap) return PG_OK;

    size_t new_cap = buf->len * 2;
    if (new_cap < 64) new_cap = 64;
    uint8_t *new_data = realloc(buf->data, new_cap);
    if (!new_data) return PG_ERR_NOMEM;
    buf->data = new_data;
    buf->cap = new_cap;
    return PG_OK;
}
