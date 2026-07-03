#ifndef PG_PARSER_H
#define PG_PARSER_H

#include "parsegraph/types.h"
#include "ast.h"

typedef struct pg_parse_ctx {
    const uint8_t *input;
    uint32_t       len;
    uint32_t       pos;
    pg_label_id  (*intern_label)(void *ctx, const char *s, uint32_t len);
    void          *intern_ctx;
} pg_parse_ctx;

pg_status    pg_parse_query(pg_parse_ctx *ctx, pg_ast_node **out);
pg_ast_node *pg_parse_pattern(pg_parse_ctx *ctx);
pg_ast_node *pg_parse_filter(pg_parse_ctx *ctx);
pg_ast_node *pg_parse_expression(pg_parse_ctx *ctx);

#endif
