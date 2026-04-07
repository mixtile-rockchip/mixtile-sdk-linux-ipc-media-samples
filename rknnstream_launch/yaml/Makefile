# Makefile for YAML Parser

# 编译器和工具
CC = gcc
CROSS_CC_32 = arm-rockchip1240-linux-gnueabihf-gcc
CROSS_CC_64 = aarch64-rockchip1240-linux-gnu-gcc

# 编译选项
CFLAGS = -Wall -Wextra -I.
LDFLAGS =

# 源文件
YAML_SRC = yaml_parse.c
TEST_SRC = yaml_test.c
EXAMPLE_SRC = example_usage.c

# 目标文件
YAML_OBJ = $(YAML_SRC:.c=.o)
TEST_BIN = yaml_test
EXAMPLE_BIN = example_usage
CROSS_TEST_BIN = yaml_test_arm
CROSS_EXAMPLE_BIN = example_arm

# 默认目标
all: $(TEST_BIN) $(EXAMPLE_BIN)

# 编译测试程序
$(TEST_BIN): $(YAML_OBJ) $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译示例程序
$(EXAMPLE_BIN): $(YAML_OBJ) $(EXAMPLE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 交叉编译（ARM 32-bit）
cross-arm32:
	$(CROSS_CC_32) $(CFLAGS) -o $(CROSS_TEST_BIN) $(TEST_SRC) $(YAML_SRC) $(LDFLAGS)
	$(CROSS_CC_32) $(CFLAGS) -o $(CROSS_EXAMPLE_BIN) $(EXAMPLE_SRC) $(YAML_SRC) $(LDFLAGS)

# 交叉编译（ARM 64-bit）
cross-arm64:
	$(CROSS_CC_64) $(CFLAGS) -o $(CROSS_TEST_BIN) $(TEST_SRC) $(YAML_SRC) $(LDFLAGS)
	$(CROSS_CC_64) $(CFLAGS) -o $(CROSS_EXAMPLE_BIN) $(EXAMPLE_SRC) $(YAML_SRC) $(LDFLAGS)

# 运行测试
test: $(TEST_BIN)
	./$(TEST_BIN) config.yaml

# 运行示例
example: $(EXAMPLE_BIN)
	./$(EXAMPLE_BIN)

# 清理
clean:
	rm -f $(YAML_OBJ) $(TEST_BIN) $(EXAMPLE_BIN) $(CROSS_TEST_BIN) $(CROSS_EXAMPLE_BIN)
	rm -f yaml_test_x86 example_x86

# 安装（复制到系统目录）
install: all
	@echo "安装头文件到 /usr/local/include..."
	@mkdir -p /usr/local/include
	@cp yaml_parse.h /usr/local/include/
	@echo "安装库文件到 /usr/local/lib..."
	@mkdir -p /usr/local/lib
	@ar rcs /usr/local/lib/libyamlparse.a $(YAML_OBJ)
	@echo "安装完成！"

# 帮助信息
help:
	@echo "可用的 make 目标:"
	@echo "  all          - 编译所有程序（默认）"
	@echo "  test         - 编译并运行测试程序"
	@echo "  example      - 编译并运行示例程序"
	@echo "  cross-arm32  - 交叉编译（ARM 32-bit）"
	@echo "  cross-arm64  - 交叉编译（ARM 64-bit）"
	@echo "  clean        - 清理编译产物"
	@echo "  install      - 安装到系统目录"
	@echo "  help         - 显示此帮助信息"

.PHONY: all cross-arm32 cross-arm64 test example clean install help
