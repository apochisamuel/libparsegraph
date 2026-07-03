#include "adjacency.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_adj_init(pg_adj_list *al) {
    al->edges = NULL;
    al->count = 0;
    al->capacity = 0;
    return PG_OK;
}

void pg_adj_free(pg_adj_list *al) {
    free(al->edges);
    al->edges = NULL;
    al->count = 0;
    al->capacity = 0;
}

/*
 * BUG-4 REALLOC SITE:
 * When the adjacency array is full, we realloc to double capacity.
 * The old array is freed by realloc.  If a cursor in cursor.c
 * has cached a raw pointer into this array via cursor_follow_edge(),
 * cursor_next_neighbor() will read through the stale pointer —
 * potentially past the bounds of a differently-sized allocation
 * that now occupies the old address range.
 *
 * The missing abstraction is an AdjacencyVersion counter that cursors
 * capture and check before dereferencing cached adjacency pointers.
 */
pg_status pg_adj_append(pg_adj_list *al, pg_edge_id eid) {
    if (al->count >= al->capacity) {
        uint32_t nc = al->capacity == 0 ? PG_MAX_ADJ_INIT
                                        : al->capacity * 2;
        pg_edge_id *ne = realloc(al->edges, nc * sizeof(pg_edge_id));
        if (!ne) return PG_ERR_NOMEM;
        al->edges = ne;                           /* ← realloc: old freed */
        al->capacity = nc;
    }
    al->edges[al->count++] = eid;
    return PG_OK;
}

pg_status pg_adj_remove(pg_adj_list *al, pg_edge_id eid) {
    for (uint32_t i = 0; i < al->count; i++) {
        if (al->edges[i] == eid) {
            if (i < al->count - 1) {
                al->edges[i] = al->edges[al->count - 1];
            }
            al->count--;
            return PG_OK;
        }
    }
    return PG_ERR_NOT_FOUND;
}

bool pg_adj_contains(const pg_adj_list *al, pg_edge_id eid) {
    for (uint32_t i = 0; i < al->count; i++) {
        if (al->edges[i] == eid) return true;
    }
    return false;
}

pg_edge_id pg_adj_get(const pg_adj_list *al, uint32_t idx) {
    if (idx >= al->count) return PG_INVALID_EDGE;
    return al->edges[idx];
}

/* ── Extended adjacency operations ───────────────────────── */

/*
 * Get the capacity of the adjacency list.
 */
uint32_t pg_adj_capacity(const pg_adj_list *al) {
    return al->capacity;
}

/*
 * Get the count of edges in the adjacency list.
 */
uint32_t pg_adj_count(const pg_adj_list *al) {
    return al->count;
}

/*
 * Clear the adjacency list without freeing.
 */
void pg_adj_clear(pg_adj_list *al) {
    al->count = 0;
}

/*
 * Sort edges by edge ID.
 */
static int cmp_edge_id(const void *a, const void *b) {
    pg_edge_id ea = *(const pg_edge_id *)a;
    pg_edge_id eb = *(const pg_edge_id *)b;
    if (ea < eb) return -1;
    if (ea > eb) return 1;
    return 0;
}

void pg_adj_sort(pg_adj_list *al) {
    if (al->count < 2) return;
    qsort(al->edges, al->count, sizeof(pg_edge_id), cmp_edge_id);
}

/*
 * Binary search for an edge ID (list must be sorted).
 */
bool pg_adj_bsearch(const pg_adj_list *al, pg_edge_id eid) {
    if (al->count == 0) return false;
    uint32_t lo = 0, hi = al->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (al->edges[mid] == eid) return true;
        if (al->edges[mid] < eid) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

/*
 * Compute the intersection of two adjacency lists.
 * Assumes both are sorted. Result is allocated by caller.
 */
uint32_t pg_adj_intersect(const pg_adj_list *a, const pg_adj_list *b,
                           pg_edge_id *out, uint32_t max) {
    uint32_t ai = 0, bi = 0, count = 0;
    while (ai < a->count && bi < b->count && count < max) {
        if (a->edges[ai] == b->edges[bi]) {
            out[count++] = a->edges[ai];
            ai++; bi++;
        } else if (a->edges[ai] < b->edges[bi]) {
            ai++;
        } else {
            bi++;
        }
    }
    return count;
}

/*
 * Reserve capacity in the adjacency list.
 */
pg_status pg_adj_reserve(pg_adj_list *al, uint32_t capacity) {
    if (al->capacity >= capacity) return PG_OK;
    pg_edge_id *ne = realloc(al->edges, capacity * sizeof(pg_edge_id));
    if (!ne) return PG_ERR_NOMEM;
    al->edges = ne;
    al->capacity = capacity;
    return PG_OK;
}
