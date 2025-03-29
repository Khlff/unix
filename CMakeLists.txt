cmake_minimum_required(VERSION 3.30)
project(unix C)

set(CMAKE_C_STANDARD 11)

add_executable(unix sparse-file/sparse.c)
