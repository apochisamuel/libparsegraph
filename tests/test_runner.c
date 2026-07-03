#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* graph tests */
extern int test_graph(void);
extern int test_graph_edges(void);
extern int test_graph_properties(void);
extern int test_graph_snapshot(void);
extern int test_graph_labels(void);
/* query tests */
extern int test_query(void);
extern int test_query_with_filter(void);
extern int test_query_with_edge_traversal(void);
/* executor tests */
extern int test_executor(void);
extern int test_executor_with_edges(void);
/* memo tests */
extern int test_memo(void);
extern int test_memo_eviction(void);
extern int test_memo_hit_rate(void);
/* compact tests */
extern int test_compact(void);
extern int test_compact_analyze(void);
/* stream tests */
extern int test_stream(void);
extern int test_stream_multi_frame(void);
extern int test_stream_session_lifecycle(void);
/* index tests */
extern int test_index(void);
extern int test_index_prop(void);
extern int test_index_edge(void);
/* backtrack tests */
extern int test_backtrack(void);
extern int test_backtrack_multiple(void);
/* session tests */
extern int test_session(void);
extern int test_session_multiple_queries(void);
/* util tests */
extern int test_buffer_ops(void);
extern int test_vector_ops(void);
extern int test_map_ops(void);
extern int test_pool_ops(void);
extern int test_slab_ops(void);
extern int test_string_intern(void);

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_entry;

int main(void) {
    test_entry tests[] = {
        {"graph",              test_graph},
        {"graph_edges",        test_graph_edges},
        {"graph_properties",   test_graph_properties},
        {"graph_snapshot",     test_graph_snapshot},
        {"graph_labels",       test_graph_labels},
        {"query",              test_query},
        {"query_filter",       test_query_with_filter},
        {"query_edge",         test_query_with_edge_traversal},
        {"executor",           test_executor},
        {"executor_edges",     test_executor_with_edges},
        {"memo",               test_memo},
        {"memo_eviction",      test_memo_eviction},
        {"memo_hit_rate",      test_memo_hit_rate},
        {"compact",            test_compact},
        {"compact_analyze",    test_compact_analyze},
        {"stream",             test_stream},
        {"stream_multi",       test_stream_multi_frame},
        {"stream_session",     test_stream_session_lifecycle},
        {"index",              test_index},
        {"index_prop",         test_index_prop},
        {"index_edge",         test_index_edge},
        {"backtrack",          test_backtrack},
        {"backtrack_multi",    test_backtrack_multiple},
        {"session",            test_session},
        {"session_multi_q",    test_session_multiple_queries},
        {"buffer_ops",         test_buffer_ops},
        {"vector_ops",         test_vector_ops},
        {"map_ops",            test_map_ops},
        {"pool_ops",           test_pool_ops},
        {"slab_ops",           test_slab_ops},
        {"string_intern",      test_string_intern},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0, failed = 0;
    for (int i = 0; i < n; i++) {
        printf("Running %-22s ", tests[i].name);
        fflush(stdout);
        int r = tests[i].fn();
        if (r == 0) { printf("PASS\n"); passed++; }
        else { printf("FAIL (%d)\n", r); failed++; }
    }
    printf("\n%d/%d passed\n", passed, passed + failed);
    return failed > 0 ? 1 : 0;
}
