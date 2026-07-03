#ifndef PARSEGRAPH_STREAM_H
#define PARSEGRAPH_STREAM_H

#include "types.h"

/* Public streaming protocol API */
pg_status pg_stream_feed(void *ctx, const uint8_t *data, size_t len);
pg_status pg_stream_process(void *ctx);

#endif
