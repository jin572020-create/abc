# ToyC 第一阶段：词法分析器

这是仅面向 **第一阶段 `-token`** 的 ToyC lexer。标准输入读取 ToyC 源程序，标准输出逐行输出规定格式的 Token；错误诊断输出到标准错误。

## Ubuntu / Linux 构建方式 A：CMake

```bash
cmake -S . -B build
cmake --build build
printf 'int a = 1;\n' | ./compiler -token
```

## Ubuntu / Linux 构建方式 B：Make

```bash
make
printf 'int a = 1;\n' | ./compiler -token
```

上述两种方式都会在项目根目录生成 `compiler`。仓库不应上传预编译的 `compiler` 文件。

源码：`src/main.cpp`。当前仅实现第一阶段 `-token` 和辅助 `-dump-dfa`，**不支持后续阶段** `-ast` 或 `-asm`。

如平台仍报告“编译器异常”，请获取该提交的构建日志、执行命令、stdout、stderr、退出码；仅凭总括状态不足以确定故障原因。
