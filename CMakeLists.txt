cmake_minimum_required(VERSION 3.9)
project(c_lab5 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -lfuse")

add_executable(c_lab5 main.c)