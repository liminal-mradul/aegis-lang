cmake_minimum_required(VERSION 3.16)
project(Aegis VERSION 0.2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Threading ───────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ── Compiler flags ──────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W3 /WX- /O2 /utf-8 /wd4244 /wd4267 /wd4996)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    # MinGW / GCC / Clang
    add_compile_options(-Wall -Wextra -O2)
endif()

# ── Aegis compiler executable ───────────────────────────────────
set(AEGIS_SOURCES
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/sema.cpp
    src/interpreter.cpp
    src/codegen.cpp
)

add_executable(aegis ${AEGIS_SOURCES})
target_include_directories(aegis PRIVATE include)
target_link_libraries(aegis PRIVATE Threads::Threads)

# ── Aegis runtime library ───────────────────────────────────────
add_library(aegis_runtime STATIC runtime/aegis_runtime.c)
target_include_directories(aegis_runtime PRIVATE runtime)
if(MSVC)
    target_compile_options(aegis_runtime PRIVATE /W3 /WX-)
else()
    target_compile_options(aegis_runtime PRIVATE -Wall -O2)
    target_link_libraries(aegis_runtime m)
endif()

# ── Install ─────────────────────────────────────────────────────
install(TARGETS aegis DESTINATION bin)
install(FILES runtime/aegis_runtime.h DESTINATION include)
install(TARGETS aegis_runtime DESTINATION lib)

# ── Tests ───────────────────────────────────────────────────────
enable_testing()
add_test(NAME test_run_test3
    COMMAND aegis run ${CMAKE_SOURCE_DIR}/tests/test3.ae
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(NAME test_check_test5
    COMMAND aegis check ${CMAKE_SOURCE_DIR}/tests/test5.ae
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
