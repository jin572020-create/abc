cmake_minimum_required(VERSION 3.12)
project(toyc LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(compiler src/main.cpp)
target_compile_options(compiler PRIVATE -Wall -Wextra -Wpedantic)

# Keep header-only parser, semantics, and IR backend in dependency graph.
target_sources(compiler PRIVATE src/parser.hpp src/semantic.hpp src/ir.hpp src/native_backend.hpp src/optimizer.hpp src/ast_opt.hpp src/ast_advanced.hpp src/ssa_promote.hpp src/global_cse.hpp)
