cmake_minimum_required(VERSION 3.25)
project(linuxbee VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Test)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
add_compile_definitions(QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

enable_testing()
add_subdirectory(src/core)
add_subdirectory(src/cli)
add_subdirectory(tests)
