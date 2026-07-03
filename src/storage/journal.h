#ifndef PG_JOURNAL_H
#define PG_JOURNAL_H

#include "parsegraph/types.h"
#include "page.h"

typedef enum {
    PG_JENTRY_ADD_NODE,
    PG_JENTRY_DEL_NODE,
    PG_JENTRY_ADD_EDGE,
    PG_JENTRY_DEL_EDGE,
    PG_JENTRY_SET_PROP,
    PG_JENTRY_COMMIT,
    PG_JENTRY_ROLLBACK
} pg_journal_op;

typedef struct pg_journal_entry {
    pg_journal_op  op;
    uint64_t       entity_id;
    pg_generation  gen;
    uint32_t       data_len;
    uint8_t       *data;
} pg_journal_entry;

typedef struct pg_journal {
    pg_page_mgr   pages;
    pg_page      *current;
    uint32_t      entry_count;
    pg_generation base_gen;
} pg_journal;

pg_status pg_journal_init(pg_journal *j);
void      pg_journal_free(pg_journal *j);
pg_status pg_journal_append(pg_journal *j, const pg_journal_entry *entry);
pg_status pg_journal_commit(pg_journal *j, pg_generation gen);
void      pg_journal_reset(pg_journal *j);

#endif /* PG_JOURNAL_H */
