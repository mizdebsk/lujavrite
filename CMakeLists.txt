cmake_minimum_required(VERSION 3.15)
project(lujavrite C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

find_package(Lua REQUIRED)
find_package(JNI REQUIRED)

add_library(lujavrite SHARED lujavrite.c)

target_include_directories(lujavrite PRIVATE
    ${LUA_INCLUDE_DIR}
    ${JNI_INCLUDE_DIRS}
)

set_target_properties(lujavrite PROPERTIES
    OUTPUT_NAME "lujavrite"
    PREFIX ""
)
