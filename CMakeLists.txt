cmake_minimum_required(VERSION 3.6)
project(IHS)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

set(SOURCE_FILES MiniShell.c SmartAlloc.c)
add_executable(IHS ${SOURCE_FILES})