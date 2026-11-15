CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
.PHONY: all clean test
all: compiler
compiler: src/main.cpp src/parser.hpp src/semantic.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: compiler
	printf 'int main() { return 0; }\n' | ./compiler -ast
clean:
	rm -f compiler
