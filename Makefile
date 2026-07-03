CC      ?= clang
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11 -I include -I src
LDFLAGS :=

# Sanitizer modes
ifdef ASAN
  CFLAGS  += -fsanitize=address -fno-omit-frame-pointer -g -O1
  LDFLAGS += -fsanitize=address
endif
ifdef FUZZ
  CC       = clang
  CFLAGS  += -fsanitize=fuzzer,address -fno-omit-frame-pointer -g -O1
  LDFLAGS += -fsanitize=fuzzer,address
endif

# Source files
UTIL_SRC    := $(wildcard src/util/*.c)
CORE_SRC    := $(wildcard src/core/*.c)
STORAGE_SRC := $(wildcard src/storage/*.c)
INDEX_SRC   := $(wildcard src/index/*.c)
QUERY_SRC   := $(wildcard src/query/*.c)
EXEC_SRC    := $(wildcard src/exec/*.c)
MEMO_SRC    := $(wildcard src/memo/*.c)
STREAM_SRC  := $(wildcard src/stream/*.c)
FUZZ_SRC    := src/fuzz/fuzz_target.c

LIB_SRC := $(UTIL_SRC) $(CORE_SRC) $(STORAGE_SRC) $(INDEX_SRC) \
           $(QUERY_SRC) $(EXEC_SRC) $(MEMO_SRC) $(STREAM_SRC)
LIB_OBJ := $(LIB_SRC:.c=.o)

TEST_SRC := $(wildcard tests/*.c)
TEST_OBJ := $(TEST_SRC:.c=.o)

# Targets
.PHONY: all clean fuzz test lib

all: lib

lib: libparsegraph.a

libparsegraph.a: $(LIB_OBJ)
	ar rcs $@ $^

# Fuzz target
fuzz: CFLAGS += -fsanitize=fuzzer,address -fno-omit-frame-pointer -g -O1 -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
fuzz: LDFLAGS += -fsanitize=fuzzer,address
fuzz: $(LIB_OBJ) src/fuzz/fuzz_target.o
	$(CC) $(LDFLAGS) -o fuzz_parsegraph $^ -lm

# Standalone harness for replaying crash inputs with ASAN
harness: CFLAGS += -fsanitize=address -fno-omit-frame-pointer -g -O1
harness: LDFLAGS += -fsanitize=address
harness: $(LIB_OBJ) src/fuzz/fuzz_target.o
	$(CC) $(LDFLAGS) -o harness_replay $^ -lm

# Test binary (uses test_runner main)
test: CFLAGS += -DPG_DEBUG -g -O0
test: $(LIB_OBJ) $(TEST_OBJ)
	$(CC) $(LDFLAGS) -o test_parsegraph $^ -lm
	./test_parsegraph

# ASAN test
test-asan: CFLAGS += -fsanitize=address -fno-omit-frame-pointer -g -O1
test-asan: LDFLAGS += -fsanitize=address
test-asan: $(LIB_OBJ) $(TEST_OBJ)
	$(CC) $(LDFLAGS) -o test_parsegraph_asan $^ -lm
	./test_parsegraph_asan

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	find . -name '*.o' -delete
	rm -f libparsegraph.a fuzz_parsegraph test_parsegraph test_parsegraph_asan
