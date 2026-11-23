# A project outside the source tree that uses only the installed package:
# find_package(tmp117) and tmp117::tmp117. Run by tests/check_install.cmake.
cmake_minimum_required(VERSION 3.16)
project(tmp117_install_consumer LANGUAGES CXX)

# A consumer that builds as ISO C++20 with -pedantic-errors: the installed headers need no extension.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(tmp117 0.1 REQUIRED CONFIG)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE tmp117::tmp117)
target_compile_options(consumer PRIVATE -fno-exceptions -fno-rtti -pedantic-errors)
