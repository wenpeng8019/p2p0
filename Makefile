# P2P0 - Zero-dependency P2P Communication Library Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -I./include
LDFLAGS = 

# Windows-specific flags
ifeq ($(OS),Windows_NT)
	LDFLAGS += -lws2_32
	EXE_EXT = .exe
else
	EXE_EXT =
endif

# Source files
SRC_DIR = src
INCLUDE_DIR = include
EXAMPLES_DIR = examples
BUILD_DIR = build
BIN_DIR = bin

# Library sources
LIB_SOURCES = $(SRC_DIR)/p2p0.c \
              $(SRC_DIR)/p2p0_simple.c \
              $(SRC_DIR)/p2p0_ice_relay.c \
              $(SRC_DIR)/p2p0_pubsub.c

LIB_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SOURCES))

# Example sources
CLIENT_SIMPLE = $(BIN_DIR)/client_simple$(EXE_EXT)
CLIENT_ICE_RELAY = $(BIN_DIR)/client_ice_relay$(EXE_EXT)
CLIENT_PUBSUB = $(BIN_DIR)/client_pubsub$(EXE_EXT)
SERVER_SIMPLE = $(BIN_DIR)/server_simple$(EXE_EXT)
SERVER_ICE_RELAY = $(BIN_DIR)/server_ice_relay$(EXE_EXT)

EXAMPLES = $(CLIENT_SIMPLE) $(CLIENT_ICE_RELAY) $(CLIENT_PUBSUB) \
           $(SERVER_SIMPLE) $(SERVER_ICE_RELAY)

# Library archive
LIBP2P0 = $(BUILD_DIR)/libp2p0.a

# Default target
all: directories $(LIBP2P0) $(EXAMPLES)

# Create build directories
directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)

# Build library
$(LIBP2P0): $(LIB_OBJECTS)
	@echo "Creating static library: $@"
	ar rcs $@ $^

# Compile library source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Build examples
$(CLIENT_SIMPLE): $(EXAMPLES_DIR)/client_simple.c $(LIBP2P0)
	@echo "Building: $@"
	$(CC) $(CFLAGS) $< $(LIBP2P0) -o $@ $(LDFLAGS)

$(CLIENT_ICE_RELAY): $(EXAMPLES_DIR)/client_ice_relay.c $(LIBP2P0)
	@echo "Building: $@"
	$(CC) $(CFLAGS) $< $(LIBP2P0) -o $@ $(LDFLAGS)

$(CLIENT_PUBSUB): $(EXAMPLES_DIR)/client_pubsub.c $(LIBP2P0)
	@echo "Building: $@"
	$(CC) $(CFLAGS) $< $(LIBP2P0) -o $@ $(LDFLAGS)

$(SERVER_SIMPLE): $(EXAMPLES_DIR)/server_simple.c $(LIBP2P0)
	@echo "Building: $@"
	$(CC) $(CFLAGS) $< $(LIBP2P0) -o $@ $(LDFLAGS)

$(SERVER_ICE_RELAY): $(EXAMPLES_DIR)/server_ice_relay.c $(LIBP2P0)
	@echo "Building: $@"
	$(CC) $(CFLAGS) $< $(LIBP2P0) -o $@ $(LDFLAGS)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Run tests (basic build verification)
test: all
	@echo "Build completed successfully!"
	@echo "Library: $(LIBP2P0)"
	@echo "Examples:"
	@ls -lh $(BIN_DIR)/

# Help target
help:
	@echo "P2P0 Library Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build library and all examples (default)"
	@echo "  clean            - Remove all build artifacts"
	@echo "  test             - Build and verify"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make             - Build everything"
	@echo "  make clean       - Clean build directory"
	@echo "  make test        - Build and test"
	@echo ""
	@echo "Running examples:"
	@echo "  SIMPLE protocol:"
	@echo "    Terminal 1: ./bin/server_simple"
	@echo "    Terminal 2: ./bin/client_simple listen peer1"
	@echo "    Terminal 3: ./bin/client_simple connect peer2"
	@echo ""
	@echo "  ICE-RELAY protocol:"
	@echo "    Terminal 1: ./bin/server_ice_relay"
	@echo "    Terminal 2: ./bin/client_ice_relay offer session123"
	@echo "    Terminal 3: ./bin/client_ice_relay answer session123"
	@echo ""
	@echo "  PUBSUB protocol:"
	@echo "    ./bin/client_pubsub publish peer1 <gist_id> <token>"
	@echo "    ./bin/client_pubsub subscribe peer2 <gist_id>"

.PHONY: all clean test help directories
