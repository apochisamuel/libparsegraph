#ifndef PG_LABELS_H
#define PG_LABELS_H

#include "parsegraph/types.h"
#include "../util/string_intern.h"

typedef struct pg_label_store {
    pg_string_intern intern;
} pg_label_store;

pg_status    pg_labels_init(pg_label_store *ls);
void         pg_labels_free(pg_label_store *ls);
pg_label_id  pg_labels_intern(pg_label_store *ls, const char *name,
                               uint32_t len);
const char  *pg_labels_resolve(pg_label_store *ls, pg_label_id id);
pg_label_id  pg_labels_find(pg_label_store *ls, const char *name,
                             uint32_t len);

#endif /* PG_LABELS_H */
