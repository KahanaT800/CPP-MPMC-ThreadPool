cmake_minimum_required(VERSION 3.10)
project(threadpool VERSION 1.0.0 LANGUAGES CXX)

# Lib
add_library(threadpool STATIC
    src/thread_pool.cpp
    src/config.cpp
    src/logger.cpp
)

# C++17
target_compile_features(threadpool PUBLIC cxx_std_17)

target_include_directories(threadpool
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

# Compiler
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(threadpool PRIVATE -Wall -Wextra -Wpedantic -O2)
elseif (MSVC)
    target_compile_options(threadpool PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()

find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

target_link_libraries(threadpool
    PUBLIC
        Threads::Threads
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

enable_testing()
add_subdirectory(test)
add_subdirectory(bench)
