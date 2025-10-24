# test/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)
include(CTest)

# smoke test
add_executable(threadpool_smoke_test
    unit/smoke_test.cpp
)

target_link_libraries(threadpool_smoke_test
    PRIVATE
        GTest::gtest_main
        threadpool
)

add_test(NAME threadpool.smoke COMMAND threadpool_smoke_test)

# BoundedCircularQueue test
add_executable(bounded_circular_queue_test
    unit/bounded_queue_test.cpp
)

target_link_libraries(bounded_circular_queue_test
    PRIVATE
        GTest::gtest_main
        threadpool
)

add_test(NAME threadpool.bounded_circular_queue COMMAND bounded_circular_queue_test)

# BlockingQueueAdapter test
add_executable(blocking_queue_adapter_test
    unit/blocking_queue_adapter_test.cpp
)

target_link_libraries(blocking_queue_adapter_test
    PRIVATE
        GTest::gtest_main
        threadpool
)

add_test(NAME threadpool.blocking_queue_adapter COMMAND blocking_queue_adapter_test)

# Thread_Pool test
add_executable(thread_pool_test
    unit/thread_pool_test.cpp
)

target_link_libraries(thread_pool_test
    PRIVATE
        GTest::gtest_main
        threadpool
)

add_test(NAME threadpool.thread_pool COMMAND thread_pool_test)

# Thread pool dynamic stress test
add_executable(thread_pool_dynamic_stress_test
    unit/thread_pool_dynamic_stress_test.cpp
)

target_link_libraries(thread_pool_dynamic_stress_test
    PRIVATE
        GTest::gtest_main
        threadpool
)

add_test(NAME threadpool.thread_pool_dynamic_stress COMMAND thread_pool_dynamic_stress_test)

# Thread pool configure test
add_executable(thread_pool_cfg_test
    unit/thread_pool_cfg_test.cpp
)

target_link_libraries(thread_pool_cfg_test
    PRIVATE
        GTest::gtest_main
        threadpool
)

# Provide the source dir to the test so it can locate test_config reliably
target_compile_definitions(thread_pool_cfg_test
    PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(NAME threadpool.thread_pool_cfg_test COMMAND thread_pool_cfg_test)

# Thread pool logger test
add_executable(thread_pool_logger_test
    unit/thread_pool_logger_test.cpp
)

target_link_libraries(thread_pool_logger_test
    PRIVATE
        GTest::gtest_main
        threadpool
)

add_test(NAME threadpool.thread_pool_logger_test COMMAND thread_pool_logger_test)
