#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

namespace thread_pool {
class ThreadPool;
struct ThreadPoolConfig;
}

namespace bench_tp {

struct BenchmarkConfig {
    // Thread pool related
    std::size_t core_threads = 8;
    std::size_t max_threads = 16;
    std::size_t max_queue_size = 100000;
    std::size_t keep_alive_time_ms = 60000;
    std::string queue_full_policy = "BLOCK"; // BLOCK|DISCARD|OVERWRITE
    bool        enable_dynamic_threads = true; // Keep compatibility with current impl, mapped to thresholds
    std::size_t load_check_interval_ms = 20;
    double      scale_up_threshold = 0.8;
    double      scale_down_threshold = 0.2;
    std::size_t pending_hi = 0;   // Optional (0 means use default inference)
    std::size_t pending_low = 0;  // Optional
    std::size_t debounce_hits = 3;
    std::size_t cooldown_ms = 500;

    // Benchmark related
    std::size_t total_tasks = 1000000;
    std::size_t duration_seconds = 30;
    std::size_t warmup_seconds = 5;
    bool        use_duration_mode = true; // true: by time; false: by task count
    bool        enable_logging = true;
    bool        enable_console_output = true;
    bool        enable_real_time_monitoring = true;
    std::size_t monitoring_interval_ms = 1000;

    // Task load control (to expose bottlenecks):
    // task_work_us: per-task busy-wait (CPU) microseconds; 0 = no busy work
    // task_sleep_us: per-task sleep microseconds; 0 = no sleep
    // submit_threads: number of concurrent submitter threads in tasks mode
    std::size_t task_work_us = 0;
    std::size_t task_sleep_us = 0;
    std::size_t submit_threads = 4;

    static BenchmarkConfig LoadFromFile(const std::string& path);
};

struct BenchmarkResult {
    std::size_t tasks_completed = 0;
    double      duration_seconds = 0.0;
    double      throughput_per_second = 0.0;

    // From statistics
    std::size_t peak_threads = 0;
    std::size_t current_threads = 0;
    std::size_t active_threads = 0;

    std::size_t discarded_tasks = 0;
    std::size_t overwritten_tasks = 0;
    double      pending_ratio = 0.0;
    std::size_t pending_tasks = 0;

    // Extended statistics
    std::size_t total_submitted = 0;             // Number of tasks successfully submitted
    double      avg_exec_time_ns = 0.0;          // Average task execution time (nanoseconds)
    std::size_t peak_pending_tasks = 0;          // Observed peak queue size
};

class ThreadPoolBenchmark {
public:
    explicit ThreadPoolBenchmark(const BenchmarkConfig& cfg);

    BenchmarkResult RunBenchmark();
    void            PrintResult(const BenchmarkResult& r) const;

private:
    // Map JSON/config to current ThreadPoolConfig
    thread_pool::ThreadPoolConfig ToPoolConfig() const;

    // Two modes
    BenchmarkResult RunDurationBenchmark();
    BenchmarkResult RunTaskCountBenchmark();

    // Monitoring
    void monitoring_loop(thread_pool::ThreadPool& pool,
                         std::atomic<bool>& on,
                         std::atomic<std::size_t>& counter,
                         std::atomic<std::size_t>* peak_pending_opt = nullptr) const;

private:
    BenchmarkConfig cfg_;
};

}
