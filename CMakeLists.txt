cmake_minimum_required(VERSION 3.15)
project(lujavrite C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    _GNU_SOURCE
)
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

enable_testing()

set(LUA "lua" CACHE STRING "Lua interpreter to use in tests")

add_test(NAME test_lua
    COMMAND ${LUA} ${CMAKE_CURRENT_SOURCE_DIR}/test.lua
)

include(GNUInstallDirs)

install(TARGETS lujavrite
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/lua/${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}"
)
