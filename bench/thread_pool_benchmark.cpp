#include "thread_pool_benchmark.hpp"

#include "thread_pool/thread_pool.hpp"
#include "thread_pool/fwd.hpp"
#include "logger.hpp"

#include <thread>
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace std::chrono_literals;

namespace bench_tp {

static thread_pool::QueueFullPolicy parse_policy(const std::string& s) {
    if (s == "BLOCK" || s == "Block") return thread_pool::QueueFullPolicy::Block;
    if (s == "DISCARD" || s == "Discard") return thread_pool::QueueFullPolicy::Discard;
    if (s == "OVERWRITE" || s == "Overwrite") return thread_pool::QueueFullPolicy::Overwrite;
    return thread_pool::QueueFullPolicy::Block;
}

BenchmarkConfig BenchmarkConfig::LoadFromFile(const std::string& path) {
    BenchmarkConfig cfg;
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            std::cerr << "Warning: cannot open benchmark config file " << path << ", using defaults" << std::endl;
            return cfg;
        }
        nlohmann::json j; ifs >> j;
        if (j.contains("thread_pool")) {
            auto& p = j["thread_pool"];
            if (p.contains("core_threads")) cfg.core_threads = p["core_threads"].get<std::size_t>();
            if (p.contains("max_threads")) cfg.max_threads = p["max_threads"].get<std::size_t>();
            if (p.contains("max_queue_size")) cfg.max_queue_size = p["max_queue_size"].get<std::size_t>();
            if (p.contains("keep_alive_time_ms")) cfg.keep_alive_time_ms = p["keep_alive_time_ms"].get<std::size_t>();
            if (p.contains("queue_full_policy")) cfg.queue_full_policy = p["queue_full_policy"].get<std::string>();
            if (p.contains("enable_dynamic_threads")) cfg.enable_dynamic_threads = p["enable_dynamic_threads"].get<bool>();
            if (p.contains("load_check_interval_ms")) cfg.load_check_interval_ms = p["load_check_interval_ms"].get<std::size_t>();
            if (p.contains("scale_up_threshold")) cfg.scale_up_threshold = p["scale_up_threshold"].get<double>();
            if (p.contains("scale_down_threshold")) cfg.scale_down_threshold = p["scale_down_threshold"].get<double>();
            if (p.contains("pending_hi")) cfg.pending_hi = p["pending_hi"].get<std::size_t>();
            if (p.contains("pending_low")) cfg.pending_low = p["pending_low"].get<std::size_t>();
            if (p.contains("debounce_hits")) cfg.debounce_hits = p["debounce_hits"].get<std::size_t>();
            if (p.contains("cooldown_ms")) cfg.cooldown_ms = p["cooldown_ms"].get<std::size_t>();
        }
        if (j.contains("benchmark")) {
            auto& b = j["benchmark"];
            if (b.contains("total_tasks")) cfg.total_tasks = b["total_tasks"].get<std::size_t>();
            if (b.contains("duration_seconds")) cfg.duration_seconds = b["duration_seconds"].get<std::size_t>();
            if (b.contains("warmup_seconds")) cfg.warmup_seconds = b["warmup_seconds"].get<std::size_t>();
            if (b.contains("use_duration_mode")) cfg.use_duration_mode = b["use_duration_mode"].get<bool>();
            if (b.contains("enable_logging")) cfg.enable_logging = b["enable_logging"].get<bool>();
            if (b.contains("enable_console_output")) cfg.enable_console_output = b["enable_console_output"].get<bool>();
            if (b.contains("enable_real_time_monitoring")) cfg.enable_real_time_monitoring = b["enable_real_time_monitoring"].get<bool>();
            if (b.contains("monitoring_interval_ms")) cfg.monitoring_interval_ms = b["monitoring_interval_ms"].get<std::size_t>();
            if (b.contains("task_work_us")) cfg.task_work_us = b["task_work_us"].get<std::size_t>();
            if (b.contains("task_sleep_us")) cfg.task_sleep_us = b["task_sleep_us"].get<std::size_t>();
            if (b.contains("submit_threads")) cfg.submit_threads = b["submit_threads"].get<std::size_t>();
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: failed to parse benchmark config: " << e.what() << ", using defaults" << std::endl;
    }
    return cfg;
}

ThreadPoolBenchmark::ThreadPoolBenchmark(const BenchmarkConfig& cfg)
    : cfg_(cfg) {}

thread_pool::ThreadPoolConfig ThreadPoolBenchmark::ToPoolConfig() const {
    thread_pool::ThreadPoolConfig pcfg;
    pcfg.queue_cap = cfg_.max_queue_size;
    pcfg.core_threads = cfg_.core_threads > 0 ? cfg_.core_threads : 1;
    pcfg.max_threads = std::max(pcfg.core_threads, cfg_.max_threads);
    pcfg.keep_alive = std::chrono::milliseconds(cfg_.keep_alive_time_ms);
    pcfg.load_check_interval = std::chrono::milliseconds(cfg_.load_check_interval_ms);
    pcfg.scale_up_threshold = cfg_.enable_dynamic_threads ? cfg_.scale_up_threshold : 1.0; // When dynamic is disabled, scaling up won't trigger
    pcfg.scale_down_threshold = cfg_.enable_dynamic_threads ? cfg_.scale_down_threshold : 0.0;
    if (cfg_.pending_hi > 0) pcfg.pending_hi = cfg_.pending_hi;
    if (cfg_.pending_low > 0) pcfg.pending_low = cfg_.pending_low;
    pcfg.debounce_hits = cfg_.debounce_hits;
    pcfg.cooldown = std::chrono::milliseconds(cfg_.cooldown_ms);
    pcfg.queue_policy = parse_policy(cfg_.queue_full_policy);
    return pcfg;
}

BenchmarkResult ThreadPoolBenchmark::RunBenchmark() {
    if (cfg_.enable_console_output) {
        std::cout << "=== Thread pool throughput benchmark start ===\n"
                  << "Core threads: " << cfg_.core_threads
                  << ", Max threads: " << cfg_.max_threads
                  << ", Queue size: " << cfg_.max_queue_size << std::endl;
        if (cfg_.use_duration_mode) {
            std::cout << "Test mode: duration-based (" << cfg_.duration_seconds << " s)\n"
                      << "Warmup: " << cfg_.warmup_seconds << " s" << std::endl;
        } else {
            std::cout << "Test mode: task-count-based (" << cfg_.total_tasks << " tasks)" << std::endl;
        }
    }
    return cfg_.use_duration_mode ? RunDurationBenchmark() : RunTaskCountBenchmark();
}

void ThreadPoolBenchmark::monitoring_loop(thread_pool::ThreadPool& pool,
                                          std::atomic<bool>& on,
                                          std::atomic<std::size_t>& counter,
                                          std::atomic<std::size_t>* peak_pending_opt) const {
    using clock = std::chrono::high_resolution_clock;
    auto last_time = clock::now();
    std::size_t last_cnt = counter.load(std::memory_order_relaxed);

    while (on.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.monitoring_interval_ms));
        auto now = clock::now();
        auto cur_cnt = counter.load(std::memory_order_relaxed);
        double secs = std::chrono::duration<double>(now - last_time).count();
        double tput = secs > 0 ? (cur_cnt - last_cnt) / secs : 0.0;

        auto stats = pool.GetStatistics();
        if (peak_pending_opt) {
            auto cur = static_cast<std::size_t>(stats.statistic_pending_tasks);
            auto old = peak_pending_opt->load(std::memory_order_relaxed);
            if (cur > old) peak_pending_opt->store(cur, std::memory_order_relaxed);
        }
        if (cfg_.enable_console_output) {
            std::cout << std::fixed << std::setprecision(0)
                      << "[Monitor] Throughput: " << tput << " tasks/s, Threads: "
                      << stats.statistic_active_threads << "/" << stats.statistic_current_threads
                      << ", Queue usage: " << std::setprecision(1) << (stats.statistic_pending_ratio * 100.0) << "%"
                      << ", Completed: " << cur_cnt
                      << std::endl;
        }
        last_time = now;
        last_cnt = cur_cnt;
    }
}

BenchmarkResult ThreadPoolBenchmark::RunDurationBenchmark() {
    BenchmarkResult result{};
    auto pcfg = ToPoolConfig();
    thread_pool::ThreadPool pool(pcfg);
    pool.Start();

    // Global counter to prevent compiler optimizations
    std::atomic<std::uint64_t> global_sink{0};
    
    // Warmup
    if (cfg_.enable_console_output && cfg_.warmup_seconds > 0) {
        std::cout << "Warmup for " << cfg_.warmup_seconds << " seconds..." << std::endl;
    }
    std::atomic<std::size_t> counter{0};
    const auto warmup_end = std::chrono::high_resolution_clock::now() + std::chrono::seconds(cfg_.warmup_seconds);
    while (std::chrono::high_resolution_clock::now() < warmup_end) {
        pool.Post([&counter, &global_sink, w=cfg_.task_work_us, s=cfg_.task_sleep_us]{
            // CPU busy work - prevent optimization
            if (w > 0) {
                auto t0 = std::chrono::high_resolution_clock::now();
                std::uint64_t local_sink = 0;
                while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - t0).count() < static_cast<long long>(w)) {
                    local_sink += 1;
                }
                // Observable side effect: write to a global atomic
                global_sink.fetch_add(local_sink, std::memory_order_relaxed);
                // Memory barrier to prevent reordering
                asm volatile("" ::: "memory");
            }
            // Simulate IO wait
            if (s > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(s));
            }
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // Small wait to let the queue digest
    pool.TriggerLoadCheck();
    std::this_thread::sleep_for(200ms);

    // Wait for warmup tasks to finish; ensure stats and counters reset before benchmark
    while (pool.Pending() > 0 || pool.ActiveTasks() > 0) {
        std::this_thread::sleep_for(1ms);
    }

    // Warmup data is excluded from final stats
    pool.ResetStatistics();

    // Actual test
    counter.store(0, std::memory_order_relaxed);
    global_sink.store(0, std::memory_order_relaxed);
    auto start = std::chrono::high_resolution_clock::now();
    auto end   = start + std::chrono::seconds(cfg_.duration_seconds);

    std::atomic<bool> monitoring_on{false};
    std::atomic<std::size_t> peak_pending{0};
    std::atomic<std::size_t> submitted{0};
    std::thread monitor;
    if (cfg_.enable_real_time_monitoring) {
        monitoring_on.store(true, std::memory_order_release);
        monitor = std::thread(&ThreadPoolBenchmark::monitoring_loop, this, std::ref(pool), std::ref(monitoring_on), std::ref(counter), &peak_pending);
    }

    // Submit loop: submit tasks as fast as possible within the time window
    while (std::chrono::high_resolution_clock::now() < end) {
        pool.Post([&counter, &global_sink, w=cfg_.task_work_us, s=cfg_.task_sleep_us]{
            // CPU busy work - prevent optimization
            if (w > 0) {
                auto t0 = std::chrono::high_resolution_clock::now();
                std::uint64_t local_sink = 0;
                while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - t0).count() < static_cast<long long>(w)) {
                    local_sink += 1;
                }
                // Observable side effect: write to a global atomic
                global_sink.fetch_add(local_sink, std::memory_order_relaxed);
                // Memory barrier to prevent reordering
                asm volatile("" ::: "memory");
            }
            if (s > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(s));
            }
            counter.fetch_add(1, std::memory_order_relaxed);
        });
        submitted.fetch_add(1, std::memory_order_relaxed);
    }

    // Record the time when submissions end
    auto submit_end = std::chrono::high_resolution_clock::now();
    
    if (monitor.joinable()) {
        monitoring_on.store(false, std::memory_order_release);
        monitor.join();
    }
    
    if (cfg_.enable_console_output) {
    std::cout << "\nSubmissions done, waiting for queue to drain..." << std::endl;
    }
    
    // Graceful stop - wait for all tasks to finish
    pool.Stop(thread_pool::StopMode::Graceful);
    auto stop = std::chrono::high_resolution_clock::now();

    // Immediately fetch statistics after stop (queue should be drained)
    auto stats = pool.GetStatistics();
    
    result.tasks_completed = counter.load(std::memory_order_relaxed);
    result.duration_seconds = std::chrono::duration<double>(submit_end - start).count();
    result.throughput_per_second = result.duration_seconds > 0 ? result.tasks_completed / result.duration_seconds : 0.0;

    result.peak_threads = stats.statistic_peak_threads;
    result.current_threads = stats.statistic_current_threads;
    result.active_threads = stats.statistic_active_threads;
    result.pending_ratio = stats.statistic_pending_ratio; // Should be 0 after graceful stop
    result.pending_tasks = stats.statistic_pending_tasks;
    result.discarded_tasks = stats.statistic_discard_cnt;
    result.overwritten_tasks = stats.statistic_overwrite_cnt;
    result.total_submitted = stats.statistic_total_submitted;
    result.avg_exec_time_ns = static_cast<double>(stats.statistic_avg_exec_time.count());
    result.peak_pending_tasks = peak_pending.load(std::memory_order_relaxed);
    
    if (cfg_.enable_console_output) {
        auto drain_time = std::chrono::duration<double>(stop - submit_end).count();
        auto total_submitted_count = submitted.load(std::memory_order_relaxed);
        std::cout << "Drain phase time: " << std::fixed << std::setprecision(2) << drain_time << " s" << std::endl;
        std::cout << "Anti-optimization counter value: " << global_sink.load(std::memory_order_relaxed);
        if (cfg_.task_work_us == 0) {
            std::cout << " (empty-task mode, no CPU work)";
        }
        std::cout << std::endl;
        std::cout << "Tasks completed: " << result.tasks_completed << " / Actual submitted: " << total_submitted_count 
                  << " / Counted submitted: " << result.total_submitted << std::endl;
    }
    
    return result;
}

BenchmarkResult ThreadPoolBenchmark::RunTaskCountBenchmark() {
    BenchmarkResult result{};
    auto pcfg = ToPoolConfig();
    thread_pool::ThreadPool pool(pcfg);
    pool.Start();
    pool.ResetStatistics();

    // Global counter to prevent compiler optimizations
    std::atomic<std::uint64_t> global_sink{0};
    std::atomic<std::size_t> counter{0};
    const size_t submit_threads = cfg_.submit_threads == 0 ? 4 : cfg_.submit_threads;
    const size_t tasks_per_thread = cfg_.total_tasks / submit_threads;
    const size_t rem = cfg_.total_tasks % submit_threads;

    auto start = std::chrono::high_resolution_clock::now();

    // Progress and queue peak sampling (separate cache lines to avoid contention with workers)
    alignas(64) std::atomic<std::size_t> submitted{0};
    alignas(64) std::atomic<std::size_t> peak_pending{0};
    alignas(64) std::atomic<bool> sample_on{true};
    
    std::thread sampler([&]{
        while (sample_on.load(std::memory_order_acquire)) {
            auto s = pool.GetStatistics();
            auto cur = static_cast<std::size_t>(s.statistic_pending_tasks);
            auto old = peak_pending.load(std::memory_order_relaxed);
            if (cur > old) peak_pending.store(cur, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.monitoring_interval_ms));
        }
    });

    if (cfg_.enable_console_output) {
        std::cout << "Start submitting " << cfg_.total_tasks << " tasks..." << std::endl;
        std::cout << "Using " << submit_threads << " threads to submit tasks concurrently to improve queue utilization..." << std::endl;
    }

    const std::size_t progress_step = std::max<std::size_t>(1, cfg_.total_tasks / 200); // Print every 0.5%
    std::atomic<std::size_t> next_mark{progress_step};
    std::thread progress_printer([&]{
        while (sample_on.load(std::memory_order_acquire)) {
            auto cur = submitted.load(std::memory_order_relaxed);
            auto mark = next_mark.load(std::memory_order_relaxed);
            if (cur >= mark) {
                double pct = (static_cast<double>(cur) * 100.0) / static_cast<double>(cfg_.total_tasks);
                if (cfg_.enable_console_output) {
                    std::cout << "Submitted: " << cur << " / " << cfg_.total_tasks
                              << " (" << std::fixed << std::setprecision(3) << pct << "%)" << std::endl;
                }
                next_mark.store(mark + progress_step, std::memory_order_relaxed);
            }
            if (cur >= cfg_.total_tasks) break;
            std::this_thread::sleep_for(200ms);
        }
    });

    std::vector<std::thread> submitters;
    submitters.reserve(submit_threads);
    for (size_t t = 0; t < submit_threads; ++t) {
        size_t n = tasks_per_thread + (t == submit_threads - 1 ? rem : 0);
        submitters.emplace_back([n, &pool, &counter, &submitted, &global_sink, w=cfg_.task_work_us, s=cfg_.task_sleep_us]{
            for (size_t i = 0; i < n; ++i) {
                pool.Post([&counter, &global_sink, w, s]{
                    // CPU busy work - prevent optimization
                    if (w > 0) {
                        auto t0 = std::chrono::high_resolution_clock::now();
                        std::uint64_t local_sink = 0;
                        while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - t0).count() < static_cast<long long>(w)) {
                            local_sink += 1;
                        }
                        // Observable side effect: write to a global atomic
                        global_sink.fetch_add(local_sink, std::memory_order_relaxed);
                        // Memory barrier to prevent reordering
                        asm volatile("" ::: "memory");
                    }
                    if (s > 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(s));
                    }
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
                submitted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : submitters) th.join();
    
    // Record the time when submissions end
    auto submit_end = std::chrono::high_resolution_clock::now();

    // Submissions complete
    if (cfg_.enable_console_output) {
        auto cur = submitted.load(std::memory_order_relaxed);
        if (cur < cfg_.total_tasks) {
            double pct = (static_cast<double>(cur) * 100.0) / static_cast<double>(cfg_.total_tasks);
            std::cout << "Submitted: " << cur << " / " << cfg_.total_tasks
                      << " (" << std::fixed << std::setprecision(3) << pct << "%)" << std::endl;
        }
        std::cout << "All tasks submitted, waiting for completion (drain phase)..." << std::endl;
    }

    // End progress and sampling threads
    sample_on.store(false, std::memory_order_release);
    if (progress_printer.joinable()) progress_printer.join();
    if (sampler.joinable()) sampler.join();

    // Wait for completion -> graceful stop will wait for drain
    pool.Stop(thread_pool::StopMode::Graceful);
    auto end = std::chrono::high_resolution_clock::now();

    // Immediately fetch statistics after stop (queue should be drained)
    auto stats = pool.GetStatistics();
    
    result.tasks_completed = counter.load(std::memory_order_relaxed);
    result.duration_seconds = std::chrono::duration<double>(submit_end - start).count();
    result.throughput_per_second = result.duration_seconds > 0 ? result.tasks_completed / result.duration_seconds : 0.0;

    result.peak_threads = stats.statistic_peak_threads;
    result.current_threads = stats.statistic_current_threads;
    result.active_threads = stats.statistic_active_threads;
    result.pending_ratio = stats.statistic_pending_ratio; // Should be 0 after graceful stop
    result.pending_tasks = stats.statistic_pending_tasks;
    result.discarded_tasks = stats.statistic_discard_cnt;
    result.overwritten_tasks = stats.statistic_overwrite_cnt;
    result.total_submitted = stats.statistic_total_submitted;
    result.avg_exec_time_ns = static_cast<double>(stats.statistic_avg_exec_time.count());
    result.peak_pending_tasks = peak_pending.load(std::memory_order_relaxed);
    
    if (cfg_.enable_console_output) {
        auto drain_time = std::chrono::duration<double>(end - submit_end).count();
        std::cout << "Drain phase time: " << std::fixed << std::setprecision(2) << drain_time << " s" << std::endl;
        std::cout << "Anti-optimization counter value: " << global_sink.load(std::memory_order_relaxed);
        if (cfg_.task_work_us == 0) {
            std::cout << " (empty-task mode, no CPU work)";
        }
        std::cout << std::endl;
        std::cout << "Tasks completed: " << result.tasks_completed << " / Submitted: " << result.total_submitted << std::endl;
    }
    
    return result;
}

void ThreadPoolBenchmark::PrintResult(const BenchmarkResult& result) const {
    if (!cfg_.enable_console_output) return;
    std::cout << "\n=== Benchmark result ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2)
              << "Tasks completed: " << result.tasks_completed << "\n"
              << "Duration: " << result.duration_seconds << " s\n"
              << "Throughput: " << result.throughput_per_second << " tasks/s\n"
              << "Peak threads: " << result.peak_threads << std::endl;

    if (result.avg_exec_time_ns > 0) {
    std::cout << "Avg task time: " << std::fixed << std::setprecision(2)
          << result.avg_exec_time_ns << " ns" << std::endl;
    }

    // Queue stats
    const std::size_t cap = cfg_.max_queue_size;
    const std::size_t peak_q = result.peak_pending_tasks;
    const double peak_usage = cap > 0 ? (static_cast<double>(peak_q) * 100.0 / static_cast<double>(cap)) : 0.0;
    const double final_usage_cached = result.pending_ratio * 100.0;
    const std::size_t final_pending = result.pending_tasks;
    const double actual_final_usage = cap > 0 ? (static_cast<double>(final_pending) * 100.0 / static_cast<double>(cap)) : 0.0;

    std::cout << "\n=== Queue utilization stats ===" << std::endl;
    std::cout << std::fixed << std::setprecision(0)
              << "Queue capacity: " << cap << std::endl;
    std::cout << "Queue peak size: " << peak_q << std::endl;
    std::cout << std::fixed << std::setprecision(2)
              << "Queue peak utilization: " << peak_usage << "%" << std::endl;
    std::cout << "Final pending tasks: " << final_pending << " (should be 0 when drained)" << std::endl;
    std::cout << "Final queue utilization: " << actual_final_usage << "% (cached ratio: " 
              << final_usage_cached << "%)" << std::endl;

    if (result.discarded_tasks > 0) {
    std::cout << "Discarded tasks: " << result.discarded_tasks << std::endl;
        if (!cfg_.use_duration_mode && cfg_.total_tasks > 0) {
            double discard_rate = (static_cast<double>(result.discarded_tasks) * 100.0) / static_cast<double>(cfg_.total_tasks);
            std::cout << "Task discard rate: " << std::fixed << std::setprecision(2) << discard_rate << "%" << std::endl;
        }
    }
    if (result.overwritten_tasks > 0) {
    std::cout << "Overwritten tasks: " << result.overwritten_tasks << std::endl;
    }

    // Queue assessment
    std::cout << "\n=== Queue utilization assessment ===" << std::endl;
    std::string status = (peak_usage > 90.0) ? "High load (peak utilization >90%)" : (peak_usage > 60.0 ? "Medium load" : "Low load");
    std::cout << "Queue status: " << status << std::endl;
    if (peak_usage > 90.0) {
        std::cout << "Suggestion: consider increasing queue capacity or optimizing task processing speed" << std::endl;
    } else if (peak_usage > 60.0) {
        std::cout << "Suggestion: watch queue length during peaks; increase consumption if necessary" << std::endl;
    } else {
        std::cout << "Suggestion: current configuration looks healthy" << std::endl;
    }

    // Per-thread throughput
    if (result.peak_threads > 0) {
        double per_thread = result.throughput_per_second / static_cast<double>(result.peak_threads);
    std::cout << "\nPer-thread throughput: " << std::fixed << std::setprecision(2) << per_thread << " tasks/s/thread" << std::endl;
    }

    // Simple grade
    std::cout << std::fixed << std::setprecision(0);
    if (result.throughput_per_second > 100000) {
    std::cout << "Performance grade: Excellent (>100K tasks/s)" << std::endl;
    } else if (result.throughput_per_second > 50000) {
    std::cout << "Performance grade: Good (>50K tasks/s)" << std::endl;
    } else if (result.throughput_per_second > 10000) {
    std::cout << "Performance grade: Fair (>10K tasks/s)" << std::endl;
    } else {
    std::cout << "Performance grade: Needs optimization (<10K tasks/s)" << std::endl;
    }
}

}
