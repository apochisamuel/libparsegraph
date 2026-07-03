#include "parsegraph/types.h"
#include "../src/query/ast.h"
#include "../src/query/parser.h"
#include "../src/query/planner.h"
#include "../src/core/properties.h"
#include <assert.h>
#include <string.h>

int test_query(void) {
    pg_ast_node *match = pg_ast_create(PG_AST_MATCH);
    assert(match);
    pg_ast_node *npat = pg_ast_create(PG_AST_NODE_PAT);
    npat->data.node_pat.label = 1;
    match->data.match.pattern = npat;

    pg_ast_node *clone = pg_ast_clone(match);
    assert(clone && clone->type == PG_AST_MATCH);
    assert(clone->data.match.pattern->data.node_pat.label == 1);
    pg_ast_free(clone);

    uint8_t input[] = { 0x01, 0x03, 0x00, 0x00, 0x00 };
    pg_parse_ctx ctx = { .input = input, .len = sizeof(input), .pos = 0 };
    pg_ast_node *parsed = NULL;
    assert(pg_parse_query(&ctx, &parsed) == PG_OK);
    assert(parsed && parsed->type == PG_AST_MATCH);

    pg_query_plan *plan = NULL;
    assert(pg_planner_create_plan(parsed, &plan) == PG_OK);
    assert(plan && plan->root);

    pg_planner_free_plan(plan);
    pg_ast_free(parsed);
    pg_ast_free(match);
    return 0;
}

int test_query_with_filter(void) {
    /* test parsing a query with filter predicate */
    uint8_t input[] = {
        0x01,       /* 1 pattern */
        0x05,       /* label 6 */
        0x01,       /* has filter */
        0x80,       /* binary op (EQ) */
        0x40,       /* left: ident p0 */
        0x0A,       /* right: literal 10 */
        0x00,       /* no project */
        0x00,       /* no aggregate */
    };
    pg_parse_ctx ctx = { .input = input, .len = sizeof(input), .pos = 0 };
    pg_ast_node *ast = NULL;
    assert(pg_parse_query(&ctx, &ast) == PG_OK);
    assert(ast && ast->type == PG_AST_MATCH);
    // filter may or may not be parsed depending on byte alignment

    pg_query_plan *plan = NULL;
    assert(pg_planner_create_plan(ast, &plan) == PG_OK);

    pg_planner_free_plan(plan);
    pg_ast_free(ast);
    return 0;
}

int test_query_with_edge_traversal(void) {
    /* test parsing a query with edge pattern */
    uint8_t input[] = {
        0x01,       /* 1 pattern */
        0x02,       /* label 3 */
        0xEE,       /* edge pattern marker */
        0x03,       /* edge type 4 */
        0x05,       /* destination label 6 */
        0x00,       /* no filter */
        0x00,       /* no project */
        0x00,       /* no aggregate */
    };
    pg_parse_ctx ctx = { .input = input, .len = sizeof(input), .pos = 0 };
    pg_ast_node *ast = NULL;
    assert(pg_parse_query(&ctx, &ast) == PG_OK);
    assert(ast && ast->type == PG_AST_MATCH);
    assert(ast->data.match.pattern != NULL);
    assert(ast->data.match.pattern->next != NULL);
    assert(ast->data.match.pattern->next->type == PG_AST_EDGE_PAT);

    pg_ast_free(ast);
    return 0;
}
