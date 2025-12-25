# Lithos Storage Engine - Makefile
# 
# Build system for the Lithos LSM-tree storage engine.
# 
# Compiler: GCC (C11 standard)
# Threading: POSIX threads (pthread)
# Optimization: -O2 (balance between speed and debuggability)
# 
# Targets:
#   all      - Build library and test executable
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
          -I./src

# Linker flags
LDFLAGS := -pthread

# ============ Directories ============

SRC_DIR := src
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

# ============ Source Files ============

# Utility modules
UTIL_SOURCES := $(SRC_DIR)/util/status.c \
                $(SRC_DIR)/util/coding.c \
                $(SRC_DIR)/util/arena.c

# Main test program
MAIN_SOURCE := $(SRC_DIR)/main.c

# All object files
UTIL_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(UTIL_SOURCES))
MAIN_OBJECT := $(OBJ_DIR)/main.o

# Output binary
TEST_BINARY := $(BIN_DIR)/lithos_test

# ============ Targets ============

.PHONY: all clean test help

# Default target: build everything
all: $(TEST_BINARY)

# Build the test executable
$(TEST_BINARY): $(MAIN_OBJECT) $(UTIL_OBJECTS) | $(BIN_DIR)
	@echo "[LINK] $@"
	@$(CC) $(LDFLAGS) -o $@ $^
	@echo "Build successful: $@"

# Compile main.c
$(OBJ_DIR)/main.o: $(MAIN_SOURCE) | $(OBJ_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Compile utility sources
$(OBJ_DIR)/util/%.o: $(SRC_DIR)/util/%.c | $(OBJ_DIR)/util
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Create build directories
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(OBJ_DIR): | $(BUILD_DIR)
	@mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/util: | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/util

$(BIN_DIR): | $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)

# Build and run the test program
test: $(TEST_BINARY)
	@echo "[RUN] $(TEST_BINARY)"
	@./$(TEST_BINARY)

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
	@echo "  make all    - Build the library and test executable"
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
$(OBJ_DIR)/main.o: $(SRC_DIR)/util/status.h include/lithos/lithos_status.h \
                   $(SRC_DIR)/util/slice.h $(SRC_DIR)/util/coding.h \
                   $(SRC_DIR)/util/arena.h
