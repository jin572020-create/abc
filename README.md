# ToyC 编译器：自主实现的优化版 RISC-V64GC 后端

> 本包用于覆盖原 `abc` 仓库。**编译器的构建和运行仅依赖 C++17**，不执行 Clang/LLVM，也不需要安装 LLVM SDK。`clang`/`gcc` 仅出现在开发验证脚本中，不属于评测运行依赖。

## 1. 构建与评测接口

在仓库根目录执行：

```sh
make clean && make
make test
# 或者
cmake -S . -B build
cmake --build build
```

`make` 生成根目录的 `./compiler`；CMake 生成 `build/compiler`。评测平台若要求根目录 `./compiler`，请用 `make` 构建。

```sh
./compiler -token < input.c
./compiler -ast < input.c
./compiler -asm < input.c > input.s
./compiler -asm -opt < input.c > input.opt.s
./compiler -ir < input.c > input.ll            # 自主 LLVM IR 文本，调试接口
./compiler -ir -opt < input.c > input.opt.ll  # 优化后的 IR，调试接口
./compiler -dump-dfa
```

正常代码生成只往 stdout 打印 LLVM IR / RV64GC 汇编文本。错误诊断写 stderr；`-token`、`-ast` 行为保持与原阶段版本兼容。

## 2. 源码架构

| 文件 | 内容 |
|---|---|
| `src/main.cpp` | DFA 词法分析、命令行入口、阶段调度 |
| `src/parser.hpp` | 手写递归下降语法分析与 AST |
| `src/semantic.hpp` | 作用域、函数参数、返回值、循环语义检查 |
| `src/ir.hpp` | 手写 AST 到 LLVM IR 子集的转换 |
| `src/ast_opt.hpp` | 恒假分支清理、有限且无副作用死循环消除、无用局部声明删除 |
| `src/ast_advanced.hpp` | 纯函数调用外提、受限内联、尾递归消除、循环不变分支外提、归纳乘法强度削减 |
| `src/ssa_promote.hpp` | 非逃逸、入口已初始化的标量 alloca → SSA；循环汇合处插入 phi |
| `src/global_cse.hpp` | 基于 CFG 支配关系的跨基本块公共子表达式消除 |
| `src/optimizer.hpp` | SSA 常量折叠、复制/常量传播、块内 CSE、跨块 CSE、DCE |
| `src/native_backend.hpp` | 自主解析 IR 子集、寄存器驻留、栈帧、ABI、phi 边并行复制、指令选择与分支融合 |

## 3. 优化管线

只有 `-opt` 打开时启用以下处理：

1. 在 AST 上处理可证明安全的恒假分支、死循环与无用局部声明；对无副作用且可保证终止的短函数进行受限内联、循环不变量外提和尾递归转换；对循环不变的重复条件执行有限的 loop unswitch；对单调归纳变量的固定常数乘法进行增量维护。
2. 从 AST 构造原始、结构合法的 LLVM IR 子集，保留函数、基本块与短路求值。
3. 对入口已经初始化、地址未逃逸的局部标量进行 SSA 提升；按控制流边生成 phi，折叠简单冗余 phi。
4. 执行常量折叠/传播、复制传播、基本块内及支配安全的跨基本块 CSE、连锁 DCE。
5. 自主降低到 RV64GC：对常用 alloca / SSA 值使用独占的 s1–s11 被调用者保存寄存器，其余值仍有栈槽；处理 caller/callee 保存约定及多于八个参数；按前驱边实现 phi 并行复制；融合常见比较/条件跳转；对正的 2 的幂常量乘法/有符号取余做等价替换。

**重要限制：** 这是针对课程生成的 **LLVM IR 子集** 的自主实现，不是 LLVM 官方优化器，不支持任意 LLVM IR。SSA 提升与寄存器分配采用保守子集策略，并非完整通用 mem2reg 支配边界算法、图着色或线性扫描分配器。未实现通用 LICM、任意函数内联、常量除法魔数法、通用别名分析和两阶段完整窥孔优化。性能提升与 GCC -O2 的比值必须以平台实际运行结果为准。

## 4. 自检与测试证据

不依赖外部测试资源的基础测试：

```sh
make test
```

附带的 10 个性能测试源程序在 `tests/perf_cases/`；用于本地验证的两个可选脚本：

```sh
python3 tests/performance_rv64_vm_check.py   # 需要 GCC；使用独立 RV64 子集 VM
python3 tests/performance_differential.py   # 需要 GCC 与 Clang（只作验证）
```

上述两个脚本自动将 p04 和 p05 中的千万级/百万级固定内循环缩减到 100 次，或使用其它合法的小规模输入，以便在本地执行差分。它们**不替代希冀平台上的大规模性能测试**。完整验证记录见 `TEST_RESULTS.md`。

如果另有最初的 `cases20261110.zip`，可将其中 `codegen/functional/`、`codegen/performance/` 放到 `fixtures/codegen/`，再执行 `python3 tests/regression_extended.py` 复查 20 个功能用例。

## 5. 正式 RV64 链接与运行

本机开发验证可用 Clang 以 RISC-V 目标检查汇编格式；**交付编译器不调用它**。正式评测请使用课程的工具链及运行库：

```sh
./compiler -asm -opt < input.c > input.opt.s
riscv64-unknown-elf-gcc -march=rv64gc -mabi=lp64d -nostdlib -static \
  input.opt.s third_party/toyc/libtoyc.a -o input
qemu-riscv64 ./input < input.in > output.txt
```

原仓库已有 `tests/lexical` 可以保留；不需要将大量 `.in/.out/.exit` 文件全部上传。不要提交 `compiler`、`build/` 等编译产物。
