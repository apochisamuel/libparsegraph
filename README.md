# libparsegraph

A streaming graph query engine written in C.

## Overview

libparsegraph provides a compact, embeddable graph database engine that supports:

- **Graph storage**: Nodes and edges with labels and typed properties, backed by an arena allocator with segment-based compaction.
- **Query pipeline**: Binary query format → AST → plan → optimization → execution with pattern matching, filtering, projection, aggregation, and joins.
- **Memoization cache**: LRU-evicted cache for query result reuse with generation-based invalidation.
- **Streaming protocol**: Frame-based wire protocol supporting interleaved mutations, queries, sessions, transactions, and batch operations.

## Building

```bash
# Build static library
make lib

# Build and run tests
make test

# Build with AddressSanitizer
make test-asan ASAN=1

# Build fuzz target (requires clang)
make fuzz
```

## Architecture

```
include/parsegraph/     Public API headers (graph.h, query.h, stream.h, types.h)
src/core/               Graph CRUD, arena allocator, properties, adjacency, snapshots
src/storage/            Segment manager, compaction engine, freelist, journal
src/index/              Hash table, label/property/edge indexes
src/query/              Parser, AST, planner, optimizer, cost model
src/exec/               Executor, cursors, pattern matcher, filter, project, aggregate
src/memo/               Memoization cache, eviction, invalidation
src/stream/             Wire protocol, sessions, framing, batch operations, transactions
src/util/               Buffer, vector, map, pool, slab allocator, string interning
src/fuzz/               Fuzz target (LLVMFuzzerTestOneInput)
tests/                  Unit tests
```

## Fuzz Target

The fuzz target (`src/fuzz/fuzz_target.c`) accepts raw bytes interpreted as a stream of length-prefixed protocol frames. Each frame carries an opcode (ADD_NODE, ADD_EDGE, DELETE_NODE, SET_PROPERTY, QUERY, COMPACT, SESSION_OPEN/CLOSE, TXN_BEGIN/COMMIT, BATCH, SNAPSHOT, INDEX_REBUILD) followed by a variable-length payload.

```bash
make fuzz
./fuzz_parsegraph corpus/ -max_len=4096
```

## License

MIT
