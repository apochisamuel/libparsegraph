#include "freelist.h"
#include <stdlib.h>

pg_status pg_freelist_init(pg_freelist *fl) {
    fl->head = NULL;
    fl->count = 0;
    return PG_OK;
}

void pg_freelist_free(pg_freelist *fl) {
    pg_freelist_entry *e = fl->head;
    while (e) {
        pg_freelist_entry *next = e->next;
        free(e);
        e = next;
    }
    fl->head = NULL;
    fl->count = 0;
}

pg_status pg_freelist_push(pg_freelist *fl, uint32_t slot_idx) {
    pg_freelist_entry *e = malloc(sizeof(pg_freelist_entry));
    if (!e) return PG_ERR_NOMEM;
    e->slot_idx = slot_idx;
    e->next = fl->head;
    fl->head = e;
    fl->count++;
    return PG_OK;
}

bool pg_freelist_pop(pg_freelist *fl, uint32_t *out) {
    if (!fl->head) return false;
    pg_freelist_entry *e = fl->head;
    fl->head = e->next;
    *out = e->slot_idx;
    free(e);
    fl->count--;
    return true;
}

uint32_t pg_freelist_count(const pg_freelist *fl) {
    return fl->count;
}

/* ── Extended freelist operations ────────────────────────── */

/*
 * Check if a slot index is in the freelist.
 */
bool pg_freelist_contains(const pg_freelist *fl, uint32_t slot_idx) {
    pg_freelist_entry *e = fl->head;
    while (e) {
        if (e->slot_idx == slot_idx) return true;
        e = e->next;
    }
    return false;
}

/*
 * Clear the freelist without actually freeing associated slots.
 */
void pg_freelist_clear(pg_freelist *fl) {
    pg_freelist_entry *e = fl->head;
    while (e) {
        pg_freelist_entry *next = e->next;
        free(e);
        e = next;
    }
    fl->head = NULL;
    fl->count = 0;
}

/*
 * Drain up to N entries from the freelist into an output array.
 */
uint32_t pg_freelist_drain(pg_freelist *fl, uint32_t *out, uint32_t max) {
    uint32_t drained = 0;
    while (drained < max && fl->head) {
        pg_freelist_entry *e = fl->head;
        fl->head = e->next;
        out[drained++] = e->slot_idx;
        free(e);
        fl->count--;
    }
    return drained;
}

/*
 * Sort the freelist entries by slot index (for sequential access).
 * Rebuilds the list in sorted order.
 */
void pg_freelist_sort(pg_freelist *fl) {
    if (fl->count < 2) return;

    /* extract all entries into an array */
    uint32_t *slots = malloc(fl->count * sizeof(uint32_t));
    if (!slots) return;

    uint32_t n = 0;
    pg_freelist_entry *e = fl->head;
    while (e) {
        slots[n++] = e->slot_idx;
        e = e->next;
    }

    /* insertion sort */
    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = slots[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && slots[j] > key) {
            slots[j + 1] = slots[j];
            j--;
        }
        slots[j + 1] = key;
    }

    /* rebuild freelist in sorted order */
    pg_freelist_clear(fl);
    for (uint32_t i = 0; i < n; i++) {
        pg_freelist_push(fl, slots[i]);
    }
    free(slots);
}
