# 测试 Makefile

CC = cc
CFLAGS = -Wall -Wextra -std=c99 -g -I../include -I../src
LDFLAGS = -L../build -lp2p

# 构建目录
BUILD_DIR = build

# 测试目标
TESTS = $(BUILD_DIR)/test_transport $(BUILD_DIR)/test_simple_server $(BUILD_DIR)/test_relay_server

all: $(BUILD_DIR) $(TESTS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test_transport: test_transport.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BUILD_DIR)/test_simple_server: test_simple_server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/test_relay_server: test_relay_server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

# 运行所有测试
test: all
	@echo "\n==================================="
	@echo "Running all tests..."
	@echo "===================================\n"
	@for t in $(TESTS); do \
		echo "Running $$t..."; \
		$$t || exit 1; \
		echo ""; \
	done
	@echo "==================================="
	@echo "All tests completed successfully!"
	@echo "==================================="

clean:
	rm -rf $(BUILD_DIR) *.o *.dSYM

.PHONY: all test clean
