#include "vector.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_vector_init(pg_vector *v, size_t elem_size, uint32_t initial_cap) {
    if (initial_cap == 0) initial_cap = 8;
    v->data = malloc(elem_size * initial_cap);
    if (!v->data) return PG_ERR_NOMEM;
    v->elem_size = elem_size;
    v->count = 0;
    v->capacity = initial_cap;
    return PG_OK;
}

void pg_vector_free(pg_vector *v) {
    free(v->data);
    v->data = NULL;
    v->count = 0;
    v->capacity = 0;
}

pg_status pg_vector_ensure(pg_vector *v, uint32_t needed) {
    if (v->capacity >= needed) return PG_OK;
    uint32_t new_cap = v->capacity;
    while (new_cap < needed) new_cap *= 2;
    void *nd = realloc(v->data, v->elem_size * new_cap);
    if (!nd) return PG_ERR_NOMEM;
    v->data = nd;
    v->capacity = new_cap;
    return PG_OK;
}

pg_status pg_vector_push(pg_vector *v, const void *elem) {
    pg_status s = pg_vector_ensure(v, v->count + 1);
    if (s != PG_OK) return s;
    memcpy((char *)v->data + v->count * v->elem_size, elem, v->elem_size);
    v->count++;
    return PG_OK;
}

void *pg_vector_get(pg_vector *v, uint32_t idx) {
    if (idx >= v->count) return NULL;
    return (char *)v->data + idx * v->elem_size;
}

void *pg_vector_last(pg_vector *v) {
    if (v->count == 0) return NULL;
    return (char *)v->data + (v->count - 1) * v->elem_size;
}

pg_status pg_vector_pop(pg_vector *v, void *out) {
    if (v->count == 0) return PG_ERR_NOT_FOUND;
    v->count--;
    if (out) {
        memcpy(out, (char *)v->data + v->count * v->elem_size, v->elem_size);
    }
    return PG_OK;
}

void pg_vector_clear(pg_vector *v) {
    v->count = 0;
}

pg_status pg_vector_remove(pg_vector *v, uint32_t idx) {
    if (idx >= v->count) return PG_ERR_NOT_FOUND;
    if (idx < v->count - 1) {
        memmove(
            (char *)v->data + idx * v->elem_size,
            (char *)v->data + (idx + 1) * v->elem_size,
            (v->count - idx - 1) * v->elem_size
        );
    }
    v->count--;
    return PG_OK;
}

void *pg_vector_raw(pg_vector *v) {
    return v->data;
}

uint32_t pg_vector_count(const pg_vector *v) {
    return v->count;
}

/* ── Extended vector operations ──────────────────────────── */

/*
 * Sort the vector using a comparison function.
 * Uses insertion sort for small vectors, which is fine for our sizes.
 */
void pg_vector_sort(pg_vector *v,
                     int (*cmp)(const void *, const void *)) {
    if (v->count < 2) return;

    /* insertion sort */
    uint8_t *temp = malloc(v->elem_size);
    if (!temp) return;

    for (uint32_t i = 1; i < v->count; i++) {
        memcpy(temp, (char *)v->data + i * v->elem_size, v->elem_size);
        int32_t j = (int32_t)i - 1;
        while (j >= 0 &&
               cmp((char *)v->data + j * v->elem_size, temp) > 0) {
            memcpy((char *)v->data + (j + 1) * v->elem_size,
                   (char *)v->data + j * v->elem_size,
                   v->elem_size);
            j--;
        }
        memcpy((char *)v->data + (j + 1) * v->elem_size, temp,
               v->elem_size);
    }
    free(temp);
}

/*
 * Find an element by linear scan.
 * Returns the index, or UINT32_MAX if not found.
 */
uint32_t pg_vector_find(pg_vector *v, const void *elem,
                         int (*cmp)(const void *, const void *)) {
    for (uint32_t i = 0; i < v->count; i++) {
        void *current = (char *)v->data + i * v->elem_size;
        if (cmp(current, elem) == 0) return i;
    }
    return UINT32_MAX;
}

/*
 * Deduplicate a sorted vector in-place.
 */
void pg_vector_dedup(pg_vector *v,
                      int (*cmp)(const void *, const void *)) {
    if (v->count < 2) return;

    uint32_t write = 1;
    for (uint32_t i = 1; i < v->count; i++) {
        void *prev = (char *)v->data + (write - 1) * v->elem_size;
        void *curr = (char *)v->data + i * v->elem_size;
        if (cmp(prev, curr) != 0) {
            if (write != i) {
                memcpy((char *)v->data + write * v->elem_size,
                       curr, v->elem_size);
            }
            write++;
        }
    }
    v->count = write;
}

/*
 * Reserve capacity without adding elements.
 */
pg_status pg_vector_reserve(pg_vector *v, uint32_t cap) {
    return pg_vector_ensure(v, cap);
}

/*
 * Swap two elements.
 */
void pg_vector_swap(pg_vector *v, uint32_t a, uint32_t b) {
    if (a >= v->count || b >= v->count || a == b) return;
    uint8_t *temp = malloc(v->elem_size);
    if (!temp) return;

    void *pa = (char *)v->data + a * v->elem_size;
    void *pb = (char *)v->data + b * v->elem_size;
    memcpy(temp, pa, v->elem_size);
    memcpy(pa, pb, v->elem_size);
    memcpy(pb, temp, v->elem_size);
    free(temp);
}

/*
 * Reverse the vector in-place.
 */
void pg_vector_reverse(pg_vector *v) {
    if (v->count < 2) return;
    uint32_t left = 0, right = v->count - 1;
    while (left < right) {
        pg_vector_swap(v, left, right);
        left++;
        right--;
    }
}
