CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic

.PHONY: all clean test
all: compiler

compiler: src/main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: compiler
	printf 'int a = 1;\n' | ./compiler -token

clean:
	rm -f compiler
