# ToyC 编译器：纯 C++17 自主实现的 LLVM IR 子集与 RV64GC 后端

## 依赖与构建

**编译器运行时不依赖 Clang、LLVM、Flex、Bison、系统调用或任何外部编译器。**

- 构建：C++17 编译器（g++ 9+ 或兼容实现）、Make 或 CMake 3.12+。
- `make clean && make` 在仓库根目录生成 `./compiler`。
- `cmake -S . -B build && cmake --build build` 在 `build/compiler` 生成可执行文件。
- `python3 tests/selftest.py` 运行无需其他外部程序的自检。
- 若要运行真正 RISC-V 程序，需按照课程要求另行配置 `riscv64-unknown-elf-gcc`、`qemu-riscv64` 与 `third_party/toyc/libtoyc.a`；它们不是编译器本身的运行依赖。

## 用法

```sh
./compiler -token < input.c
./compiler -ast < input.c
./compiler -ir < input.c > input.ll             # 调试接口
./compiler -ir -opt < input.c > optimized.ll   # 调试接口
./compiler -asm < input.c > input.s            # 评测功能接口
./compiler -asm -opt < input.c > optimized.s   # 评测性能接口
./compiler -dump-dfa
```

编译器从 stdin 读 ToyC，向 stdout 输出结果；代码生成失败报告到 stderr 并返回非零退出状态。

## 架构

- `src/main.cpp`: NFA→DFA→最小化扫描器、CLI、前端串联。
- `src/parser.hpp`: 递归下降语法分析，构造 AST。
- `src/semantic.hpp`: 基础语义检查（作用域、参数数量、返回值、循环内 break/continue）。
- `src/ir.hpp`: **手写** ToyC AST→LLVM IR（文本形式，所用指令是本项目定义的支持子集）。栈上局部变量通过 `alloca/load/store` 表示；if/while、短路求值及 phi 使用基本块表示。
- `src/optimizer.hpp`: **手写** SSA 常量折叠/传播、简单复制传播、基本块内 CSE、无用纯指令 DCE。优化仅针对项目发出的有限 LLVM IR 指令子集，不是 LLVM 官方优化器。
- `src/native_backend.hpp`: **手写** LLVM IR 子集解析 + RV64GC 汇编生成。指令选择使用 `addw/subw/mulw/divw/remw` 和控制流跳转；SSA 值以独立栈槽保守分配，处理函数序言/尾声、栈 16 字节对齐、ABI 参数寄存器 `a0..a7`、第九及以后参数、函数调用返回以及 phi 入边并行拷贝。大栈帧和大偏移由扩展指令序列处理。

### 优化边界（必须如实说明）

实现了上述基础 IR 优化，**尚未实现**通用 mem2reg（支配树/支配边界）、跨基本块 CSE/GVN、LICM、内联、强度削减魔数法、图着色/线性扫描寄存器分配、两轮窥孔优化和块重排。本版本使用保守的栈槽分配，侧重正确性和不依赖外部 LLVM；在大型性能用例上可能不符合时间要求。`-opt` 是生效的，但不保证显著加速。

## 一致性和限制

- 仅支持本课程 ToyC 前端生成的 LLVM IR 子集，**不接受任意 LLVM IR**；`-ir` 输出的文本格式遵循该子集对应的 LLVM IR 语法。
- ToyC 的 `int` 按 32 位有符号整数处理，RV64 输出遵循 LP64D 的 `int` 返回与参数习惯。
- 外部 `getint`、`putint`、`getch`、`putch` 等运行时函数由课程 `libtoyc.a` 提供，不包含在这个覆盖包中。
- 不声明所有隐藏测试通过；先前的 `-ast` s05 对旧测试标准存在重复行号差异，具体参见历史记录。

## 真正执行 RV64GC 示例

```sh
./compiler -asm -opt < input.c > input.opt.s
riscv64-unknown-elf-gcc -march=rv64gc -mabi=lp64d -nostdlib -static \
  input.opt.s third_party/toyc/libtoyc.a -o input
qemu-riscv64 ./input < input.in > input.out
```

如果评测环境约定 `./compiler` 路径，请使用 `make` 构建并保证根目录可执行文件在该路径。
