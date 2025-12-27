# Lithos Storage Engine - Makefile
# 
# Build system for the Lithos LSM-tree storage engine.
# 
# Compiler: GCC (C11 standard)
# Threading: POSIX threads (pthread)
# Optimization: -O2 (balance between speed and debuggability)
# 
# Targets:
#   all      - Build library, tests, and CLI
#   clean    - Remove all build artifacts
#   test     - Build and run the test program
#   help     - Display this help message
# 
# Author: Aditya (@bit2swaz)

# ============ Compiler Configuration ============

# Use GCC for C11 compliance and GNU extensions
CC := gcc

# Compiler flags (strict mode for production-grade code)
CFLAGS := -std=c11 \
          -g \
          -Wall \
          -Wextra \
          -Werror \
          -Wpedantic \
          -Wformat=2 \
          -Wstrict-prototypes \
          -Wmissing-prototypes \
          -O2 \
          -pthread \
          -I./include \
          -I./src \
          -I./tests

# Linker flags
LDFLAGS := -pthread

# Enable sanitizers when SANITIZE=1
ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
endif

# ============ Directories ============

SRC_DIR := src
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

# ============ Source Files ============

# Library sources (core engine)
LIB_SOURCES := $(SRC_DIR)/util/status.c \
               $(SRC_DIR)/util/coding.c \
               $(SRC_DIR)/util/arena.c \
               $(SRC_DIR)/util/crc32c.c \
               $(SRC_DIR)/util/env_posix.c \
               $(SRC_DIR)/util/compression.c \
               $(SRC_DIR)/util/options.c \
               $(SRC_DIR)/util/bloom.c \
               $(SRC_DIR)/util/cache.c \
               $(SRC_DIR)/core/skiplist.c \
               $(SRC_DIR)/core/dbformat.c \
               $(SRC_DIR)/core/memtable.c \
               $(SRC_DIR)/core/log_writer.c \
               $(SRC_DIR)/core/log_reader.c \
               $(SRC_DIR)/core/write_batch.c \
               $(SRC_DIR)/core/table_cache.c \
               $(SRC_DIR)/core/db_impl.c \
               $(SRC_DIR)/core/table/block_builder.c \
               $(SRC_DIR)/core/table/format.c \
               $(SRC_DIR)/core/table/table_builder.c \
               $(SRC_DIR)/core/table/block.c \
               $(SRC_DIR)/core/table/table.c \
               $(SRC_DIR)/core/table/filter_block.c \
               $(SRC_DIR)/core/table/merger.c \
               $(SRC_DIR)/core/version_edit.c \
               $(SRC_DIR)/core/version_set.c

# Test sources
TEST_SOURCES := tests/lithos_test_main.c \
                tests/test_coding.c \
                tests/test_arena.c \
                tests/test_skiplist.c \
                tests/test_memtable.c \
                tests/test_wal.c \
                tests/test_block_builder.c \
                tests/test_table_builder.c \
                tests/test_table_reader.c \
                tests/test_bloom.c \
                tests/test_cache.c \
                tests/test_version_set.c \
                tests/test_db.c

# CLI sources
CLI_SOURCES := tools/lithos_cli.c

# Stress tool sources
STRESS_SOURCES := tools/lithos_stress.c

# Fuzz tool sources
FUZZ_SOURCES := tools/lithos_fuzz.c

# All object files
LIB_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LIB_SOURCES))
TEST_OBJECTS := $(patsubst tests/%.c,$(OBJ_DIR)/tests/%.o,$(TEST_SOURCES))
CLI_OBJECTS := $(patsubst tools/%.c,$(OBJ_DIR)/tools/%.o,$(CLI_SOURCES))
STRESS_OBJECTS := $(patsubst tools/%.c,$(OBJ_DIR)/tools/%.o,$(STRESS_SOURCES))
FUZZ_OBJECTS := $(patsubst tools/%.c,$(OBJ_DIR)/tools/%.o,$(FUZZ_SOURCES))

# Output files
LIB_ARCHIVE := $(BUILD_DIR)/liblithos.a
TEST_BINARY := $(BIN_DIR)/lithos_test
CLI_BINARY := $(BIN_DIR)/lithos_cli
STRESS_BINARY := $(BIN_DIR)/lithos_stress
FUZZ_BINARY := $(BIN_DIR)/lithos_fuzz

# ============ Targets ============

.PHONY: all clean test help stress fuzz sanitize

# Default target: build everything
all: $(LIB_ARCHIVE) $(TEST_BINARY) $(CLI_BINARY) $(STRESS_BINARY) $(FUZZ_BINARY)

# Build the static library
$(LIB_ARCHIVE): $(LIB_OBJECTS) | $(BUILD_DIR)
	@echo "[AR] $@"
	@ar rcs $@ $^
	@echo "Library built: $@"

# Build the test executable
$(TEST_BINARY): $(TEST_OBJECTS) $(LIB_ARCHIVE)
	@mkdir -p $(BIN_DIR)
	@echo "[LINK] $@"
	@$(CC) $(LDFLAGS) -o $@ $(TEST_OBJECTS) -L$(BUILD_DIR) -llithos
	@echo "Build successful: $@"

# Build the CLI executable
$(CLI_BINARY): $(CLI_OBJECTS) $(LIB_ARCHIVE)
	@mkdir -p $(BIN_DIR)
	@echo "[LINK] $@"
	@$(CC) $(LDFLAGS) -o $@ $(CLI_OBJECTS) -L$(BUILD_DIR) -llithos
	@echo "Build successful: $@"

# Build the stress executable
$(STRESS_BINARY): $(STRESS_OBJECTS) $(LIB_ARCHIVE)
	@mkdir -p $(BIN_DIR)
	@echo "[LINK] $@"
	@$(CC) $(LDFLAGS) -o $@ $(STRESS_OBJECTS) -L$(BUILD_DIR) -llithos
	@echo "Build successful: $@"

# Build the fuzz executable
$(FUZZ_BINARY): $(FUZZ_OBJECTS) $(LIB_ARCHIVE)
	@mkdir -p $(BIN_DIR)
	@echo "[LINK] $@"
	@$(CC) $(LDFLAGS) -o $@ $(FUZZ_OBJECTS) -L$(BUILD_DIR) -llithos
	@echo "Build successful: $@"

# Compile library sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Compile test sources
$(OBJ_DIR)/tests/%.o: tests/%.c | $(OBJ_DIR)/tests
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Compile CLI sources
$(OBJ_DIR)/tools/%.o: tools/%.c | $(OBJ_DIR)/tools
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Create build directories
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(OBJ_DIR): | $(BUILD_DIR)
	@mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/tests: | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/tests

$(OBJ_DIR)/tools: | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/tools

# Build and run the test program
test: $(TEST_BINARY)
	@echo "[RUN] $(TEST_BINARY)"
	@./$(TEST_BINARY)

# Build the stress gauntlet tool
stress: $(STRESS_BINARY)

# Build the fuzz corruption tester
fuzz: $(FUZZ_BINARY)

# Build with sanitizers enabled
sanitize:
	@$(MAKE) clean
	@$(MAKE) SANITIZE=1 all

# Clean all build artifacts
clean:
	@echo "[CLEAN] Removing build directory..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete."

# Display help message
help:
	@echo "Lithos Storage Engine - Build System"
	@echo ""
	@echo "Usage:"
	@echo "  make all    - Build the library, tests, and CLI"
	@echo "  make test   - Build and run the test program"
	@echo "  make clean  - Remove all build artifacts"
	@echo "  make help   - Display this help message"
	@echo ""
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"

# ============ Dependency Tracking ============

# Auto-generate header dependencies
# This ensures that changing a .h file triggers recompilation of dependent .c files

# Note: For a full build system, we'd use -MMD -MP flags to generate .d files
# For this initial version, we'll add explicit dependencies for key headers

$(OBJ_DIR)/util/status.o: $(SRC_DIR)/util/status.h include/lithos/lithos_status.h
$(OBJ_DIR)/util/coding.o: $(SRC_DIR)/util/coding.h
$(OBJ_DIR)/util/arena.o: $(SRC_DIR)/util/arena.h
$(OBJ_DIR)/tests/lithos_test_main.o: tests/all_tests.h tests/testharness.h
