#include "error.h"
#include <stdio.h>
#include <stdarg.h>

static const char *status_names[] = {
    [PG_OK]                = "OK",
    [PG_ERR_NOMEM]         = "out of memory",
    [PG_ERR_NOT_FOUND]     = "not found",
    [PG_ERR_EXISTS]        = "already exists",
    [PG_ERR_INVALID]       = "invalid argument",
    [PG_ERR_OVERFLOW]      = "overflow",
    [PG_ERR_FULL]          = "capacity full",
    [PG_ERR_IO]            = "I/O error",
    [PG_ERR_PROTOCOL]      = "protocol error",
    [PG_ERR_TXN_CONFLICT]  = "transaction conflict",
    [PG_ERR_SESSION_CLOSED]= "session closed",
    [PG_ERR_INTERNAL]      = "internal error",
};

const char *pg_status_str(pg_status s) {
    if ((unsigned)s < sizeof(status_names) / sizeof(status_names[0]) &&
        status_names[s]) {
        return status_names[s];
    }
    return "unknown error";
}

void pg_log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[parsegraph ERROR] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void pg_log_debug(const char *fmt, ...) {
#ifdef PG_DEBUG
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[parsegraph DBG] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
#else
    (void)fmt;
#endif
}
