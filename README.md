# ToyC Lexer — Stage 1

第一阶段 ToyC 词法分析器。标准输入读取源代码，`-token` 时按测试样例要求在标准输出打印 Token。源码为 `src/main.cpp`，实现了 Thompson 风格的 NFA 片段、子集构造 DFA 和保持 Token 标签的 DFA 最小化。

## 编译与运行

Linux / WSL，根目录运行：

```bash
make
./compiler -token < tests/lexical/l01_keywords.c
make test
```

也支持 CMake，保留原仓库的默认产物路径（不擅自改动）：

```bash
cmake -S . -B build
cmake --build build
./build/compiler -token < tests/lexical/l01_keywords.c
python3 tests/run_tests.py --compiler ./build/compiler
```

> `compiler` 是构建出的二进制文件，**不要提交**。请使用评测平台要求的构建方案和入口。Makefile 默认在根目录产生 `./compiler`，CMake 默认在 `build/` 下产生 `compiler`。本次未擅自修改上述路径约定。

## 词法格式（以 `cases20261110.zip` 的 30 组样例为准）

- `序号:类型:"原始词素"`，整数也加引号；例如 `0:IntConst:"000000"`。
- 连续数字按一个 `IntConst` 匹配，保留前导零，不在词法阶段做数值转换。
- 运算符采用最长匹配；关键字优先于同长度标识符。
- 空白、`//`、`/*...*/` 注释不产生 Token。
- 若有非法字符：输出 `reject`、逐条 `零基行号 illegal character '字符'`，空一行后继续输出合法 Token。单引号使用双引号包围；反斜杠表示为 `\\`；UTF-8 非 ASCII 字符按完整字符报错一次。
- 错误用例返回非零退出码，合法输入返回 0。

**约定差异**：较早实验指导文字将 `IntConst` 记录为无引号、数字规则写为 `0|[1-9][0-9]*`。2026-11-10 提供的标准 `.token` 文件明确使用带引号的整数，并要求 `000000` 是单个 Token。本版本严格依据该批测试的标准输出。若希冀实际评测采用另一套规范，则需核对评测版本。

## 测试

```bash
make test
```

内置 30 组原样词法 `.c` 与 `.token` 对照，测试逐字节比对 stdout（预先规范化参考文件的 CRLF 为 LF），检查成功/失败退出码及 stderr。该测试**不**代表第二、三、四阶段：`-ast`、`-asm` 尚未实现。
