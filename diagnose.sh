#!/usr/bin/env bash
set -u
printf '%s\n' '== toolchain =='
cmake --version | head -1
c++ --version | head -1
printf '%s\n' '== clean cmake build =='
rm -rf build
if ! cmake -S . -B build; then echo 'CMAKE CONFIG FAILED'; exit 10; fi
if ! cmake --build build; then echo 'CMAKE BUILD FAILED'; exit 11; fi
printf '%s\n' '== binary =='
ls -l compiler
file compiler || true
printf '%s\n' '== basic lexical smoke test =='
printf 'int a = 1;\n' | ./compiler -token > /tmp/toyc_stdout.txt 2>/tmp/toyc_stderr.txt
ret=$?
printf 'exit_code=%s\n' "$ret"
printf '%s\n' 'stdout:'
cat /tmp/toyc_stdout.txt
printf '%s\n' 'stderr:'
cat /tmp/toyc_stderr.txt
if [ "$ret" -ne 0 ]; then exit 12; fi
printf '%s\n' '== optional make test =='
make clean && make test
