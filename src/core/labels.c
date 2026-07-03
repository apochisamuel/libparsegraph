#include "labels.h"

pg_status pg_labels_init(pg_label_store *ls) {
    return pg_intern_init(&ls->intern, 64);
}

void pg_labels_free(pg_label_store *ls) {
    pg_intern_free(&ls->intern);
}

pg_label_id pg_labels_intern(pg_label_store *ls, const char *name,
                              uint32_t len) {
    if (len > PG_MAX_LABEL_LEN) len = PG_MAX_LABEL_LEN;
    return (pg_label_id)pg_intern_put(&ls->intern, name, len);
}

const char *pg_labels_resolve(pg_label_store *ls, pg_label_id id) {
    return pg_intern_get(&ls->intern, (uint32_t)id);
}

pg_label_id pg_labels_find(pg_label_store *ls, const char *name,
                            uint32_t len) {
    if (len > PG_MAX_LABEL_LEN) len = PG_MAX_LABEL_LEN;
    return (pg_label_id)pg_intern_find(&ls->intern, name, len);
}
