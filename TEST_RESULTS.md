# 本地测试记录（2026-11-24）

## 本次实际执行与结果

- **构建**：GCC 14.2.0，`make -B` 成功；CMake 3.31.6，`cmake -S . -B build && cmake --build build` 成功。
- **LLVM**：Clang 17.0.0，支持 `-target riscv64-unknown-elf -march=rv64gc -mabi=lp64d`。
- **第五/六部分接口**：`-ir` 输出可被 LLVM 解析的 IR；`-ir -opt` 可生成优化后的 IR；`-asm` 和 `-asm -opt` 都能输出 RISC-V64GC 汇编。
- **目标文件语法检查**：对测试包中 20 个功能源程序及 10 个性能源程序，分别执行 `-asm` 与 `-asm -opt`，均经 Clang 的 RISC-V 目标汇编器生成 `.o`：**60/60**。
- **结果正确性**：同一批 30 个源程序的 LLVM IR 在本机用 Clang 优化并编译为 x86-64 执行文件，与一份测试用 ToyC I/O 运行时一起链接。20 个功能输入和 30 个性能输入，**50/50** 的 stdout 和 exit code 与测试数据一致。
- **原词法回归**：30/30 `.token` 输出与测试包中的标准文件字节一致（未将非法输入退出码是否符合测试程序要求计入）。
- **原语法与语义回归**：24/25 与旧 `.check-ast` 的首行/错误行号完全一致；唯一差异 `s05_while_without_condition`（旧预期两个 `2`，当前输出一个 `2`）。
- **轻量自测**：`make test` 和 `tests/smoke.py` 成功，包括 4 个 AST/IR 样例和 8 次 RV64 目标汇编器检查。

## 测试证据边界

运行环境中没有 `qemu-riscv64`，也没有课程要求的 `third_party/toyc/libtoyc.a`；因此没有执行课程指定的 **RV64GC 汇编→链接→QEMU 运行** 的完整闭环。以上 50/50 是主机执行 LLVM IR 的结果，不等价于 QEMU 上的 50/50。实际希冀平台也没有被调用，不能保证隐藏测试通过。

## 本实现的优化归属

`-opt` 执行 Clang/LLVM 的 `-O2`，优化器会根据 LLVM 版本及可适用条件执行相关内存到 SSA 提升、传播、CSE/GVN、DCE、循环、内联和目标优化。项目自身提供 AST→LLVM IR 发射器和调用 LLVM 的驱动代码；不宣称本项目手写了 mem2reg、图着色、线性扫描等算法。
