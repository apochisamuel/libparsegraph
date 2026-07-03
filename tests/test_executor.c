#include "parsegraph/graph.h"
#include "../src/exec/executor.h"
#include "../src/query/parser.h"
#include "../src/query/planner.h"
#include <assert.h>

int test_executor(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id lbl = pg_graph_intern_label(g, "Node", 4);
    pg_graph_add_node(g, lbl);
    pg_graph_add_node(g, lbl);
    pg_graph_add_node(g, lbl);

    uint8_t input[] = { 0x01, (uint8_t)(lbl - 1), 0x00, 0x00, 0x00 };
    pg_parse_ctx ctx = { .input = input, .len = sizeof(input), .pos = 0 };
    pg_ast_node *ast = NULL;
    assert(pg_parse_query(&ctx, &ast) == PG_OK);

    pg_query_plan *plan = NULL;
    assert(pg_planner_create_plan(ast, &plan) == PG_OK);

    pg_executor *exec = NULL;
    assert(pg_executor_create(&exec, g, plan) == PG_OK);
    assert(pg_executor_run(exec) == PG_OK);

    pg_executor_destroy(exec);
    pg_planner_free_plan(plan);
    pg_ast_free(ast);
    pg_graph_destroy(g);
    return 0;
}

int test_executor_with_edges(void) {
    pg_graph *g;
    assert(pg_graph_create(&g) == PG_OK);

    pg_label_id person = pg_graph_intern_label(g, "Person", 6);
    pg_label_id knows = pg_graph_intern_label(g, "KNOWS", 5);
    pg_node_id n1 = pg_graph_add_node(g, person);
    pg_node_id n2 = pg_graph_add_node(g, person);
    pg_node_id n3 = pg_graph_add_node(g, person);
    pg_graph_add_edge(g, n1, n2, knows);
    pg_graph_add_edge(g, n2, n3, knows);

    uint8_t input[] = { 0x01, (uint8_t)(person - 1), 0x00, 0x00, 0x00 };
    pg_parse_ctx ctx = { .input = input, .len = sizeof(input), .pos = 0 };
    pg_ast_node *ast = NULL;
    assert(pg_parse_query(&ctx, &ast) == PG_OK);

    pg_query_plan *plan = NULL;
    assert(pg_planner_create_plan(ast, &plan) == PG_OK);

    pg_executor *exec = NULL;
    assert(pg_executor_create(&exec, g, plan) == PG_OK);
    assert(pg_executor_run(exec) == PG_OK);
    assert(exec->result.node_count >= 3);

    pg_executor_destroy(exec);
    pg_planner_free_plan(plan);
    pg_ast_free(ast);
    pg_graph_destroy(g);
    return 0;
}
