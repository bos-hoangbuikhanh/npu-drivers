# Match the existing BOS NPU test application's target compiler.
CC = aarch64-linux-gnu-gcc

# KERNEL_SRC may be overridden when this directory is built externally.
KERNEL_SRC ?= ../../../../..
BUILD_DIR := build
TARGET := $(BUILD_DIR)/npu_se_test
SOURCES := src/npu_se_test.c src/npu_transport.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)

# Use only exported kernel UAPI headers; the utility does not link driver code.
CPPFLAGS += -D__EXPORTED_HEADERS__ -I$(KERNEL_SRC)/include/uapi -Isrc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11

all: $(TARGET)

$(BUILD_DIR):
	# Keep all generated objects and the binary outside the source directory.
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c src/npu_transport.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	# Remove every generated artifact for a reproducible rebuild.
	rm -rf $(BUILD_DIR)

.PHONY: all clean
