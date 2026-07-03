#!/bin/bash -eu

# Build library sources only (exclude fuzz/ and tests/)
for src in $(find $SRC -path '*/src/*.c' ! -path '*/fuzz/*'); do
    $CC $CFLAGS -I $SRC/include -I $SRC/src \
        -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION \
        -c "$src" -o "$(basename "$src" .c).o"
done

# Build fuzz target
$CC $CFLAGS -I $SRC/include -I $SRC/src \
    -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION \
    -c $SRC/src/fuzz/fuzz_target.c -o fuzz_target.o

# Link — do NOT glob tests/*.o, only lib + fuzz_target
$CXX $CFLAGS \
    adjacency.o aggregate.o arena.o ast.o backtrack.o batch.o buffer.o \
    cache.o compact.o cost.o cursor.o edge_index.o entry.o error.o \
    eviction.o executor.o filter.o framing.o freelist.o generation.o \
    graph.o hash.o invalidation.o join.o journal.o label_index.o \
    labels.o map.o matcher.o optimizer.o page.o parser.o plan_ops.o \
    planner.o pool.o project.o prop_index.o properties.o protocol.o \
    refcount.o segment.o session.o slab.o snapshot.o string_intern.o \
    txn.o vector.o \
    fuzz_target.o \
    $LIB_FUZZING_ENGINE \
    -lm -o $OUT/fuzz_parsegraph
