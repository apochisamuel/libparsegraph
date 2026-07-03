#ifndef PG_BACKTRACK_H
#define PG_BACKTRACK_H

#include "parsegraph/types.h"
#include "cursor.h"
#include "matcher.h"
#include "../core/snapshot.h"

/*
 * Backtracking state for pattern matching with speculative branches.
 * BUG-3: checkpoints capture snapshot references that may be freed.
 */

typedef struct pg_backtrack_point {
    pg_snapshot    *snapshot;
    pg_node_id      cursor_node;
    uint32_t        match_depth;
    pg_match_result saved_result;
    bool            active;
} pg_backtrack_point;

typedef struct pg_backtrack_stack {
    pg_backtrack_point *points;
    uint32_t            count;
    uint32_t            capacity;
} pg_backtrack_stack;

pg_status pg_backtrack_init(pg_backtrack_stack *bs);
void      pg_backtrack_free(pg_backtrack_stack *bs);
pg_status pg_backtrack_save(pg_backtrack_stack *bs, pg_snapshot *snap,
                             pg_cursor *cur, uint32_t depth,
                             pg_match_result *result);
pg_status pg_backtrack_restore(pg_backtrack_stack *bs, pg_cursor *cur,
                                uint32_t *depth, pg_match_result *result);
bool      pg_backtrack_has_points(const pg_backtrack_stack *bs);

#endif
