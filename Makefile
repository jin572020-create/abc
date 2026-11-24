CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
.PHONY: all clean test
all: compiler
compiler: src/main.cpp src/parser.hpp src/semantic.hpp src/ir.hpp src/native_backend.hpp src/optimizer.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: compiler
	printf 'int main() { return 0; }\n' | ./compiler -ast
	python3 tests/selftest.py
clean:
	rm -f compiler
