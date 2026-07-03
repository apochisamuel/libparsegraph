#include "join.h"
#include <stdlib.h>

pg_status pg_join_init(pg_join_ctx *jc) {
    pg_status s = pg_map_init(&jc->build_table, 64);
    if (s != PG_OK) return s;
    jc->join_key = PG_INVALID_NODE;
    jc->build_count = 0;
    jc->probe_count = 0;
    jc->output_count = 0;
    return PG_OK;
}

void pg_join_free(pg_join_ctx *jc) {
    pg_map_free(&jc->build_table);
}

pg_status pg_join_build(pg_join_ctx *jc, pg_match_result *results) {
    for (uint32_t i = 0; i < results->node_count; i++) {
        pg_node *n = results->nodes[i];
        if (n) {
            pg_map_put(&jc->build_table, (uint64_t)n->id, n);
            jc->build_count++;
        }
    }
    return PG_OK;
}

pg_status pg_join_probe(pg_join_ctx *jc, pg_match_result *probe,
                         pg_match_result *output) {
    for (uint32_t i = 0; i < probe->node_count; i++) {
        pg_node *n = probe->nodes[i];
        if (!n) continue;

        /* check outgoing edges for join matches */
        for (uint32_t e = 0; e < n->outgoing.count; e++) {
            pg_node_id dst = n->outgoing.edges[e];  /* edge id, not node id */
            pg_node *match = pg_map_get(&jc->build_table, (uint64_t)dst);
            if (match) {
                pg_match_result_add_node(output, n);
                pg_match_result_add_node(output, match);
                jc->output_count++;
            }
        }
        jc->probe_count++;
    }
    return PG_OK;
}

/* ── Extended hash join ──────────────────────────────────── */

/*
 * Build the hash table from a set of nodes, keyed on a property.
 */
pg_status pg_join_build_on_prop(pg_join_ctx *jc, pg_match_result *results,
                                 pg_prop_key key) {
    for (uint32_t i = 0; i < results->node_count; i++) {
        pg_node *n = results->nodes[i];
        if (!n) continue;

        pg_value *v = pg_props_get(&n->properties, key);
        if (!v || v->type != PG_VAL_INT) continue;

        uint64_t hash_key = (uint64_t)v->v.i;
        pg_map_put(&jc->build_table, hash_key, n);
        jc->build_count++;
    }
    return PG_OK;
}

/*
 * Probe phase: for each node in the probe set, look up matching
 * nodes in the build table using a property value.
 */
pg_status pg_join_probe_on_prop(pg_join_ctx *jc, pg_match_result *probe,
                                 pg_prop_key key, pg_match_result *output) {
    for (uint32_t i = 0; i < probe->node_count; i++) {
        pg_node *n = probe->nodes[i];
        if (!n) continue;

        pg_value *v = pg_props_get(&n->properties, key);
        if (!v || v->type != PG_VAL_INT) continue;

        uint64_t hash_key = (uint64_t)v->v.i;
        pg_node *match = pg_map_get(&jc->build_table, hash_key);
        if (match) {
            pg_match_result_add_node(output, n);
            pg_match_result_add_node(output, match);
            jc->output_count++;
        }
        jc->probe_count++;
    }
    return PG_OK;
}

/*
 * Semi-join: returns nodes from the probe set that have at least
 * one match in the build table, without duplicating the build side.
 */
pg_status pg_join_semi(pg_join_ctx *jc, pg_match_result *probe,
                        pg_match_result *output) {
    for (uint32_t i = 0; i < probe->node_count; i++) {
        pg_node *n = probe->nodes[i];
        if (!n) continue;

        bool has_match = false;
        for (uint32_t e = 0; e < n->outgoing.count; e++) {
            pg_node_id dst = n->outgoing.edges[e];
            if (pg_map_has(&jc->build_table, (uint64_t)dst)) {
                has_match = true;
                break;
            }
        }
        if (has_match) {
            pg_match_result_add_node(output, n);
            jc->output_count++;
        }
        jc->probe_count++;
    }
    return PG_OK;
}

/*
 * Anti-join: returns nodes from the probe set that have NO match
 * in the build table.
 */
pg_status pg_join_anti(pg_join_ctx *jc, pg_match_result *probe,
                        pg_match_result *output) {
    for (uint32_t i = 0; i < probe->node_count; i++) {
        pg_node *n = probe->nodes[i];
        if (!n) continue;

        bool has_match = false;
        for (uint32_t e = 0; e < n->outgoing.count; e++) {
            pg_node_id dst = n->outgoing.edges[e];
            if (pg_map_has(&jc->build_table, (uint64_t)dst)) {
                has_match = true;
                break;
            }
        }
        if (!has_match) {
            pg_match_result_add_node(output, n);
            jc->output_count++;
        }
        jc->probe_count++;
    }
    return PG_OK;
}
