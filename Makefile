CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS ?= -Iinclude
BUILD_DIR := build
TARGET := $(BUILD_DIR)/jcl-sim
SOURCES := src/main.c src/jcl_sim.c src/jcl_crypto.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test: $(TARGET)
	./tests/smoke.sh

clean:
	rm -rf $(BUILD_DIR)
