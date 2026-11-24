# ToyC 编译器：词法、语法、语义、LLVM IR 与 RV64GC 代码生成

本包在原 `abc` 仓库上增量覆盖，不必创建新仓库。保留既有 `-token`、`-ast` 与 `-dump-dfa`，新增 `-ir`、`-ir -opt`、`-asm`、`-asm -opt`。源码基于 C++17。

## 核心实现与工具依赖

| 文件 | 功能 |
| --- | --- |
| `src/main.cpp` | ToyC 词法器及统一编译入口，沿用原有接口 |
| `src/parser.hpp` | 递归下降 Parser，构造 AST |
| `src/semantic.hpp` | 基础语义检查 |
| `src/ir.hpp` | AST → LLVM IR，函数/基本块/局部栈槽/表达式/短路条件/循环/调用 |
| `src/llvm_driver.hpp` | 调用外部 Clang/LLVM 将 IR 优化并降低为 RV64GC 汇编 |
| `CMakeLists.txt`、`Makefile` | 独立构建脚本 |

**新增运行依赖：Clang/LLVM，需支持 `riscv64-unknown-elf` 目标。** 这不是纯手写汇编后端：LLVM 负责 `-O2` 优化、RISC-V 指令选择、寄存器分配、spill/reload 和汇编输出。C++ 项目本身可以用 GCC 14 或其他 C++17 编译器构建，但在运行 `-asm`、`-asm -opt`、`-ir -opt` 时必须能够执行 `clang`。仅有 `g++`、`riscv64-unknown-elf-gcc` 不等于已满足运行依赖。若评测平台不预装 Clang，运行会失败，请先确认平台的工具白名单。可通过 `TOYC_CLANG=/path/to/clang` 指定 LLVM 工具链位置。

项目使用手写 NFA→DFA→最小化词法器和手写递归下降 Parser，不使用 Flex、Bison、ANTLR，也没有 .l/.y 生成步骤。LLVM IR 是自定义 AST 发射器输出的正式 LLVM IR 文本，经 Clang 的优化与后端转换。

## 构建

```sh
# Ubuntu / Linux
clang --version
clang -print-targets | grep riscv
cmake -S . -B build
cmake --build build
# 如果需要根目录可执行文件：
make
```

`make` 生成 `./compiler`；`cmake` 生成 `./build/compiler`。和原仓库的构建路径保持一致。不要把任何二进制文件上传到 GitHub。

## 运行

```sh
./compiler -token < input.c
./compiler -ast < input.c
./compiler -ir < input.c > input.ll
./compiler -ir -opt < input.c > input.opt.ll
./compiler -asm < input.c > input.s
./compiler -asm -opt < input.c > input.opt.s
```

`-asm` 使用 LLVM 的 `-O0` 后端路径；`-asm -opt` 使用 `-O2`，包括 LLVM 的 SSA 提升、传播、DCE、CFG 简化、循环优化和后端优化等。不是声称本项目手写了这些 pass。`-ir` 是可选调试入口，并非平台功能测试所需。输出文件只包含 LLVM IR 或汇编；Clang 的诊断进入 stderr。`-ast` 保持 `accept/reject + 0 起始错误行号` 的格式。

## LLVM IR 生成约定

1. 函数开始时为形参、局部变量分别在 `entry` 生成 `alloca i32`；形参存入自己的栈槽。局部变量声明在作用域栈中登记，读取 `load`，赋值 `store`。
2. 表达式使用 i32 算术，关系运算先 `icmp` 再 `zext i1 to i32`；逻辑 `&&` / `||` 使用短路分支，作为值使用时以 `phi i32` 汇合。
3. `if`、`while`、`break`、`continue`、`return` 使用显式基本块和终结指令；全部块以 `br`、`ret` 或 `unreachable` 结尾。
4. 调用形参按源代码中的从左到右顺序求值，内置运行时函数按 ABI 签名声明。所有函数签名预先收集，以支持前向调用。
5. 局部作用域支持遮蔽；语义检查若未通过，编译器停止代码生成。

## 链接运行（需要课程提供的库与 QEMU）

```sh
./compiler -asm < input.c > input.s
./compiler -asm -opt < input.c > input.opt.s
riscv64-unknown-elf-gcc -march=rv64gc -mabi=lp64d \
  -nostdlib -static input.opt.s third_party/toyc/libtoyc.a -o input
qemu-riscv64 ./input < input.in > result.out
```

若工具链未提供 `qemu-riscv64`、`riscv64-unknown-elf-gcc` 或 `third_party/toyc/libtoyc.a`，请按课程指定环境安装或放入对应文件；本压缩包不包含第三方运行时库。

## 测试与已知边界

见 `TEST_RESULTS.md`。已核对 30 个代码生成测试源程序的 IR、`-asm` / `-asm -opt` RV64 目标汇编器语法，以及在主机 LLVM 后端运行的 50 组输入；**尚未在 QEMU + 课程运行库组合下执行 RISC-V 最终程序**。

旧版 25 个语法/语义用例与标准结果比较有 24 个完全一致，剩余 `s05_while_without_condition` 的旧标准要求 `reject,2,2`，本项目当前输出 `reject,2`；平台先前反馈重复行号可能导致错误输出，因此不恢复重复报错。

原词法器的整数规则在源码中为 `[0-9]+`，与早期指导材料的 `0|[1-9][0-9]*` 不同；需按最新修订规则确认。本项目仅涵盖给定的 ToyC 子集，不支持数组声明、全局变量或 C 的全部语法。
