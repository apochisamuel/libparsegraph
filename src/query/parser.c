#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static bool at_end(pg_parse_ctx *ctx) {
    return ctx->pos >= ctx->len;
}

static uint8_t peek(pg_parse_ctx *ctx) {
    if (at_end(ctx)) return 0;
    return ctx->input[ctx->pos];
}

static uint8_t advance(pg_parse_ctx *ctx) {
    if (at_end(ctx)) return 0;
    return ctx->input[ctx->pos++];
}

static void skip_ws(pg_parse_ctx *ctx) {
    while (!at_end(ctx) && isspace(peek(ctx))) advance(ctx);
}

static bool read_u8(pg_parse_ctx *ctx, uint8_t *out) {
    if (ctx->pos >= ctx->len) return false;
    *out = ctx->input[ctx->pos++];
    return true;
}

static bool read_u16(pg_parse_ctx *ctx, uint16_t *out) {
    if (ctx->pos + 2 > ctx->len) return false;
    *out = (uint16_t)ctx->input[ctx->pos] |
           ((uint16_t)ctx->input[ctx->pos + 1] << 8);
    ctx->pos += 2;
    return true;
}

static bool read_u32(pg_parse_ctx *ctx, uint32_t *out) {
    if (ctx->pos + 4 > ctx->len) return false;
    memcpy(out, ctx->input + ctx->pos, 4);
    ctx->pos += 4;
    return true;
}

/*
 * Binary query format (from fuzz input):
 *   [match_count:1] [node_patterns...] [has_filter:1] [filter_expr]
 *   [has_project:1] [column_count:1] [columns...] [has_aggregate:1]
 *   [agg_func:1] [agg_column:1]
 */
pg_ast_node *pg_parse_pattern(pg_parse_ctx *ctx) {
    uint8_t label_raw;
    if (!read_u8(ctx, &label_raw)) return NULL;

    pg_ast_node *pat = pg_ast_create(PG_AST_NODE_PAT);
    if (!pat) return NULL;

    pg_label_id label = (pg_label_id)(label_raw % 16) + 1;
    pat->data.node_pat.label = label;
    pat->data.node_pat.alias = NULL;
    pat->data.node_pat.alias_len = 0;

    /* check for edge pattern following */
    if (!at_end(ctx) && peek(ctx) == 0xEE) {
        advance(ctx);
        uint8_t edge_type_raw;
        if (read_u8(ctx, &edge_type_raw)) {
            pg_ast_node *edge = pg_ast_create(PG_AST_EDGE_PAT);
            if (edge) {
                edge->data.edge_pat.edge_type = (pg_label_id)(edge_type_raw % 8) + 1;
                edge->data.edge_pat.directed = true;
                pat->next = edge;

                /* parse destination node pattern */
                pg_ast_node *dst = pg_parse_pattern(ctx);
                if (dst) edge->next = dst;
            }
        }
    }
    return pat;
}

pg_ast_node *pg_parse_expression(pg_parse_ctx *ctx) {
    uint8_t expr_type;
    if (!read_u8(ctx, &expr_type)) return NULL;

    if (expr_type < 0x40) {
        /* literal integer */
        pg_ast_node *lit = pg_ast_create(PG_AST_LITERAL);
        if (!lit) return NULL;
        lit->data.literal.value.type = PG_VAL_INT;
        lit->data.literal.value.v.i = (int64_t)expr_type;
        return lit;
    } else if (expr_type < 0x80) {
        /* identifier (property reference) */
        pg_ast_node *id = pg_ast_create(PG_AST_IDENT);
        if (!id) return NULL;
        uint8_t name_idx = expr_type & 0x3F;
        id->data.ident.name = malloc(8);
        if (id->data.ident.name) {
            snprintf(id->data.ident.name, 8, "p%u", name_idx);
            id->data.ident.len = (uint32_t)strlen(id->data.ident.name);
        }
        return id;
    } else {
        /* binary operation */
        pg_ast_node *binop = pg_ast_create(PG_AST_BINOP);
        if (!binop) return NULL;
        binop->data.binop.op = (pg_binop)((expr_type >> 4) & 0x07);
        binop->data.binop.left = pg_parse_expression(ctx);
        binop->data.binop.right = pg_parse_expression(ctx);
        return binop;
    }
}

pg_ast_node *pg_parse_filter(pg_parse_ctx *ctx) {
    pg_ast_node *f = pg_ast_create(PG_AST_FILTER);
    if (!f) return NULL;
    f->data.filter.predicate = pg_parse_expression(ctx);
    f->data.filter.selectivity = 0.5f;
    f->data.filter.column_refs = 1;
    return f;
}

pg_status pg_parse_query(pg_parse_ctx *ctx, pg_ast_node **out) {
    pg_ast_node *match = pg_ast_create(PG_AST_MATCH);
    if (!match) return PG_ERR_NOMEM;

    /* parse patterns */
    uint8_t pat_count;
    if (!read_u8(ctx, &pat_count)) { pg_ast_free(match); return PG_ERR_INVALID; }
    pat_count = (pat_count % 4) + 1;

    pg_ast_node *first_pat = NULL;
    pg_ast_node *last_pat = NULL;
    for (uint8_t i = 0; i < pat_count && !at_end(ctx); i++) {
        pg_ast_node *pat = pg_parse_pattern(ctx);
        if (!pat) continue;
        if (!first_pat) { first_pat = pat; last_pat = pat; }
        else {
            /* find tail of current pat chain */
            pg_ast_node *t = last_pat;
            while (t->next) t = t->next;
            t->next = pat;
            last_pat = pat;
        }
    }
    match->data.match.pattern = first_pat;

    /* optional filter */
    uint8_t has_filter = 0;
    if (read_u8(ctx, &has_filter) && (has_filter & 1)) {
        match->data.match.where_clause = pg_parse_filter(ctx);
    }

    /* build the query root: MATCH → optional FILTER → PROJECT/AGG */
    pg_ast_node *root = match;

    uint8_t has_project = 0;
    if (read_u8(ctx, &has_project) && (has_project & 1)) {
        pg_ast_node *proj = pg_ast_create(PG_AST_PROJECT);
        if (proj) {
            uint8_t col_count;
            if (read_u8(ctx, &col_count)) {
                col_count = (col_count % 4) + 1;
                proj->data.project.columns = malloc(col_count * sizeof(uint32_t));
                if (proj->data.project.columns) {
                    for (uint8_t i = 0; i < col_count; i++) {
                        uint8_t col;
                        if (read_u8(ctx, &col))
                            proj->data.project.columns[i] = col;
                    }
                    proj->data.project.column_count = col_count;
                }
            }
            /* chain after match */
            pg_ast_node *t = root;
            while (t->next) t = t->next;
            t->next = proj;
        }
    }

    uint8_t has_agg = 0;
    if (read_u8(ctx, &has_agg) && (has_agg & 1)) {
        pg_ast_node *agg = pg_ast_create(PG_AST_AGGREGATE);
        if (agg) {
            uint8_t func_raw, col_raw;
            if (read_u8(ctx, &func_raw)) agg->data.aggregate.func = func_raw % 5;
            if (read_u8(ctx, &col_raw))  agg->data.aggregate.column = col_raw;
            pg_ast_node *t = root;
            while (t->next) t = t->next;
            t->next = agg;
        }
    }

    *out = root;
    return PG_OK;
}

/* ── Extended parser ─────────────────────────────────────── */

/*
 * Parse a binary-encoded property value from the query input.
 */
static pg_value parse_value(pg_parse_ctx *ctx) {
    pg_value val;
    memset(&val, 0, sizeof(val));

    uint8_t type_tag;
    if (!read_u8(ctx, &type_tag)) return val;

    switch (type_tag % 4) {
    case 0: {
        val.type = PG_VAL_INT;
        uint8_t lo, hi;
        if (read_u8(ctx, &lo) && read_u8(ctx, &hi)) {
            val.v.i = (int64_t)((uint16_t)lo | ((uint16_t)hi << 8));
        }
        break;
    }
    case 1:
        val.type = PG_VAL_BOOL;
        {
            uint8_t bval;
            if (read_u8(ctx, &bval)) val.v.b = bval & 1;
        }
        break;
    case 2:
        val.type = PG_VAL_FLOAT;
        val.v.f = 0.0;
        {
            uint8_t raw;
            if (read_u8(ctx, &raw)) val.v.f = (double)raw / 255.0;
        }
        break;
    case 3: {
        val.type = PG_VAL_STRING;
        uint8_t slen;
        if (read_u8(ctx, &slen)) {
            slen = slen % 32;
            val.v.str.data = malloc(slen + 1);
            if (val.v.str.data) {
                for (uint8_t i = 0; i < slen && !at_end(ctx); i++) {
                    val.v.str.data[i] = (char)advance(ctx);
                }
                val.v.str.data[slen] = '\0';
                val.v.str.len = slen;
            }
        }
        break;
    }
    }
    return val;
}

/*
 * Parse a join clause from the query input.
 */
static pg_ast_node *parse_join(pg_parse_ctx *ctx) {
    pg_ast_node *join = pg_ast_create(PG_AST_JOIN);
    if (!join) return NULL;

    uint8_t join_key;
    if (read_u8(ctx, &join_key)) {
        join->data.join.join_key = join_key;
    }

    /* parse left and right patterns */
    join->data.join.left = pg_parse_pattern(ctx);
    join->data.join.right = pg_parse_pattern(ctx);

    return join;
}

/*
 * Parse a function call expression (COUNT, SUM, etc.).
 */
static pg_ast_node *parse_func_call(pg_parse_ctx *ctx) {
    pg_ast_node *fc = pg_ast_create(PG_AST_FUNC_CALL);
    if (!fc) return NULL;

    uint8_t func_id;
    if (read_u8(ctx, &func_id)) {
        /* wrap as an aggregate for simplicity */
        fc->type = PG_AST_AGGREGATE;
        fc->data.aggregate.func = func_id % 5;

        uint8_t col;
        if (read_u8(ctx, &col)) {
            fc->data.aggregate.column = col;
        }
    }
    return fc;
}

/*
 * Extended query format with join and function calls.
 */
pg_status pg_parse_query_extended(pg_parse_ctx *ctx, pg_ast_node **out) {
    uint8_t query_type;
    if (!read_u8(ctx, &query_type)) return PG_ERR_INVALID;

    switch (query_type % 3) {
    case 0:
        /* standard MATCH query */
        return pg_parse_query(ctx, out);

    case 1: {
        /* join query */
        pg_ast_node *match = pg_ast_create(PG_AST_MATCH);
        if (!match) return PG_ERR_NOMEM;
        match->data.match.pattern = pg_parse_pattern(ctx);

        pg_ast_node *join = parse_join(ctx);
        if (join) {
            pg_ast_node *t = match;
            while (t->next) t = t->next;
            t->next = join;
        }

        *out = match;
        return PG_OK;
    }

    case 2: {
        /* aggregation query */
        pg_ast_node *match = pg_ast_create(PG_AST_MATCH);
        if (!match) return PG_ERR_NOMEM;
        match->data.match.pattern = pg_parse_pattern(ctx);

        pg_ast_node *agg = parse_func_call(ctx);
        if (agg) {
            pg_ast_node *t = match;
            while (t->next) t = t->next;
            t->next = agg;
        }

        *out = match;
        return PG_OK;
    }
    }
    return PG_ERR_INVALID;
}
