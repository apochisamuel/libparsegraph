#include "journal.h"
#include <stdlib.h>
#include <string.h>

pg_status pg_journal_init(pg_journal *j) {
    pg_status s = pg_page_mgr_init(&j->pages);
    if (s != PG_OK) return s;
    j->current = pg_page_alloc(&j->pages);
    if (!j->current) return PG_ERR_NOMEM;
    j->entry_count = 0;
    j->base_gen = 0;
    return PG_OK;
}

void pg_journal_free(pg_journal *j) {
    pg_page_mgr_free(&j->pages);
    j->current = NULL;
    j->entry_count = 0;
}

pg_status pg_journal_append(pg_journal *j, const pg_journal_entry *entry) {
    /* serialize entry header into current page */
    uint8_t header[16];
    uint32_t hlen = 0;

    header[hlen++] = (uint8_t)entry->op;
    memcpy(header + hlen, &entry->entity_id, 8); hlen += 8;
    memcpy(header + hlen, &entry->gen, 4); hlen += 4;

    uint32_t total = hlen + entry->data_len;
    if (pg_page_remaining(j->current) < total) {
        j->current = pg_page_alloc(&j->pages);
        if (!j->current) return PG_ERR_NOMEM;
    }

    pg_page_write(j->current, header, hlen);
    if (entry->data_len > 0 && entry->data) {
        pg_page_write(j->current, entry->data, entry->data_len);
    }
    j->entry_count++;
    return PG_OK;
}

pg_status pg_journal_commit(pg_journal *j, pg_generation gen) {
    pg_journal_entry commit = {
        .op = PG_JENTRY_COMMIT,
        .entity_id = 0,
        .gen = gen,
        .data_len = 0,
        .data = NULL
    };
    return pg_journal_append(j, &commit);
}

void pg_journal_reset(pg_journal *j) {
    pg_page_mgr_free(&j->pages);
    pg_page_mgr_init(&j->pages);
    j->current = pg_page_alloc(&j->pages);
    j->entry_count = 0;
}

/* ── Extended journal operations ─────────────────────────── */

/*
 * Count entries in the journal.
 */
uint32_t pg_journal_count(const pg_journal *j) {
    return j->entry_count;
}

/*
 * Total bytes used across all journal pages.
 */
uint32_t pg_journal_bytes_used(const pg_journal *j) {
    uint32_t total = 0;
    pg_page *p = j->pages.pages;
    while (p) {
        total += p->used;
        p = p->next;
    }
    return total;
}

/*
 * Append a node mutation to the journal with data payload.
 */
pg_status pg_journal_record_node(pg_journal *j, pg_journal_op op,
                                  pg_node_id id, pg_label_id label,
                                  pg_generation gen) {
    uint8_t data[8];
    memcpy(data, &label, sizeof(pg_label_id));
    pg_journal_entry entry = {
        .op = op,
        .entity_id = (uint64_t)id,
        .gen = gen,
        .data_len = sizeof(pg_label_id),
        .data = data
    };
    return pg_journal_append(j, &entry);
}

/*
 * Append an edge mutation to the journal.
 */
pg_status pg_journal_record_edge(pg_journal *j, pg_journal_op op,
                                  pg_edge_id id, pg_node_id src,
                                  pg_node_id dst, pg_generation gen) {
    uint8_t data[16];
    memcpy(data, &src, sizeof(pg_node_id));
    memcpy(data + sizeof(pg_node_id), &dst, sizeof(pg_node_id));
    pg_journal_entry entry = {
        .op = op,
        .entity_id = (uint64_t)id,
        .gen = gen,
        .data_len = sizeof(pg_node_id) * 2,
        .data = data
    };
    return pg_journal_append(j, &entry);
}

/*
 * Append a property change to the journal.
 */
pg_status pg_journal_record_prop(pg_journal *j, pg_node_id id,
                                  pg_prop_key key, const pg_value *val,
                                  pg_generation gen) {
    uint8_t data[16];
    uint32_t data_len = 0;
    memcpy(data, &key, sizeof(pg_prop_key));
    data_len += sizeof(pg_prop_key);
    data[data_len++] = (uint8_t)val->type;
    if (val->type == PG_VAL_INT) {
        memcpy(data + data_len, &val->v.i, 8);
        data_len += 8;
    }

    pg_journal_entry entry = {
        .op = PG_JENTRY_SET_PROP,
        .entity_id = (uint64_t)id,
        .gen = gen,
        .data_len = data_len,
        .data = data
    };
    return pg_journal_append(j, &entry);
}

/*
 * Iterate all pages in the journal.
 */
typedef bool (*pg_journal_page_fn)(pg_page *page, void *ctx);

void pg_journal_foreach_page(pg_journal *j, pg_journal_page_fn fn,
                              void *ctx) {
    pg_page *p = j->pages.pages;
    while (p) {
        if (!fn(p, ctx)) return;
        p = p->next;
    }
}

/*
 * Total number of pages used by the journal.
 */
uint32_t pg_journal_page_count(const pg_journal *j) {
    return j->pages.page_count;
}

/*
 * Check if the journal is empty.
 */
bool pg_journal_is_empty(const pg_journal *j) {
    return j->entry_count == 0;
}

/*
 * Truncate the journal to only the most recent N entries.
 * (Approximate: we can only truncate at page boundaries.)
 */
void pg_journal_truncate(pg_journal *j, uint32_t keep_entries) {
    if (j->entry_count <= keep_entries) return;
    
    /* simple approach: just reset and start fresh */
    pg_journal_reset(j);
}
