#!/bin/bash

# YAML 解析器演示脚本

echo "=========================================="
echo "    YAML 解析器功能演示"
echo "=========================================="
echo ""

# 检查编译产物
if [ ! -f "./yaml_test" ]; then
    echo "❌ 未找到 yaml_test，正在编译..."
    make all
    if [ $? -ne 0 ]; then
        echo "❌ 编译失败"
        exit 1
    fi
fi

echo "📋 1. 显示配置文件内容"
echo "=========================================="
cat config.yaml
echo ""
echo ""

echo "🧪 2. 运行测试套件"
echo "=========================================="
./yaml_test config.yaml
TEST_RESULT=$?
echo ""

echo ""
echo "📖 3. 运行使用示例"
echo "=========================================="
./example_usage
echo ""

echo ""
echo "📦 4. 编译产物信息"
echo "=========================================="
echo "x86_64 版本:"
file yaml_test example_usage | grep "x86-64"
echo ""
echo "ARM 32-bit 版本（需要 ARM 设备运行）:"
file yaml_test_arm example_arm | grep "ARM"
echo ""

echo "✅ 交叉编译完成！"
echo ""
echo "ARM 版本可执行文件："
echo "  - yaml_test_arm   (测试程序)"
echo "  - example_arm     (使用示例)"
echo ""
echo "部署到 ARM 设备："
echo "  adb push yaml_test_arm /oem/usr/bin/"
echo "  adb push example_arm /oem/usr/bin/"
echo "  adb push config.yaml /oem/usr/etc/"
echo ""

if [ $TEST_RESULT -eq 0 ]; then
    echo "🎉 所有测试通过！"
else
    echo "⚠️  部分测试失败"
fi

echo "=========================================="
