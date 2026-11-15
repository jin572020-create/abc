CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
.PHONY: all clean test
all: compiler
compiler: src/main.cpp src/parser.hpp src/semantic.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: compiler
	python3 tests/run_tests.py --compiler ./compiler
	python3 tests/run_syntax.py --compiler ./compiler
clean:
	rm -f compiler
