#ifndef PG_ERROR_H
#define PG_ERROR_H

#include "parsegraph/types.h"

const char *pg_status_str(pg_status s);
void pg_log_error(const char *fmt, ...);
void pg_log_debug(const char *fmt, ...);

#ifdef PG_DEBUG
  #define PG_DBG(...) pg_log_debug(__VA_ARGS__)
#else
  #define PG_DBG(...) ((void)0)
#endif

#define PG_CHECK(expr)                          \
    do {                                        \
        pg_status _s = (expr);                  \
        if (_s != PG_OK) return _s;             \
    } while (0)

#define PG_CHECK_NULL(ptr)                      \
    do {                                        \
        if (!(ptr)) return PG_ERR_NOMEM;        \
    } while (0)

#endif /* PG_ERROR_H */
