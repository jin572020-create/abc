# ToyC 编译器 · 第一阶段词法 + 第二阶段语法与语义

本项目继续在原仓库内实现，不需要新建仓库。使用 C++17，无 Flex/Bison/ANTLR 依赖。

## 构建

在 Linux/Ubuntu 的仓库根目录运行：

```bash
make clean && make
# 或：
cmake -S . -B build && cmake --build build
```

- `make` 输出 `./compiler`；
- `cmake` 默认输出 `./build/compiler`。不要提交任何 `compiler` 二进制文件或 `build/`。
- 提交平台要求的可执行路径以平台实际配置为准；本项目没有改变原仓库的 CMake 和 Make 输出目录约定。

## 使用

```bash
./compiler -token < input.c > input.token
./compiler -ast < input.c > input.check-ast
```

`-token`：保留第一阶段 Token 输出格式。出现词法非法字符时输出 `reject`、诊断和合法 Token。

`-ast`：从 stdin 读取 ToyC 源程序，完成词法、递归下降语法分析和已实现的基础语义检查。
成功输出 `accept`；失败输出 `reject`，后面按升序列出错误的 **0 基**行号，允许重复行号。错误文本为可选，本项目不输出。
两种接口的正常执行均返回码 `0`，stderr 不写测试数据。

## 实现方式

- `src/main.cpp`：第一阶段 Thompson 风格 NFA→DFA→DFA 最小化词法器和统一驱动。
- `src/parser.hpp`：第二阶段递归下降解析器，构建内部 AST（Program、Function、Block、Decl、Assign、Binary、Unary、Call、Return 等），保留源码位置。
- `src/semantic.hpp`：语义检查，包括局部词法作用域、变量未声明与重复声明、形参数量、void 值被使用、循环外 break/continue。
- 表达式使用按优先级分层的循环解析（等价于消除左递归后的尾部产生式），保证乘除高于加减、同级运算符左结合；if/else 遵循最近 if 原则。
- 在语句错误处通过 `;`、`}` 或控制关键字同步，避免死循环和部分级联诊断。

### 左递归的等价改写

原文法 `AddExpr -> AddExpr (+|-) MulExpr | MulExpr`，等价于
`AddExpr -> MulExpr AddExprTail`、
`AddExprTail -> (+|-) MulExpr AddExprTail | ε`。
乘除、关系、逻辑与/或层同理。实现以循环从左向右构造 `Binary` 节点，保持左结合。

## 回归测试

```bash
make test
# 或
python3 tests/run_tests.py --compiler ./compiler
python3 tests/run_syntax.py --compiler ./compiler
```

测试样例来源于课程所提供的 `cases20261110.zip`。词法 30 例、语法与语义 25 例。比较前仅统一测试答案文件的 CRLF/LF 换行，解析结果按要求比较首行与行号。

## 阶段范围

本次交付完成 `-token`、`-ast` 与基础语义检查，不包含第三、第四阶段的 `-ir`、`-asm`、RV64GC 代码生成，也不宣称覆盖全部未公开隐藏用例。
