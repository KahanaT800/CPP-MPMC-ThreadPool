cmake_minimum_required(VERSION 3.10)

project(thread_pool_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(thread_pool_benchmark
    main.cpp
    thread_pool_benchmark.cpp
)

# Our library target is defined at the root CMakeLists as `threadpool`
# It already carries include directories and links to nlohmann_json, spdlog, Threads
find_package(Threads REQUIRED)

# Link against the library in this repo
target_link_libraries(thread_pool_benchmark PRIVATE threadpool Threads::Threads)

# Put the binary under build/bench for convenience
set_target_properties(thread_pool_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench
)