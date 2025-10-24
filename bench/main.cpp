#include "thread_pool_benchmark.hpp"

#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

#include <iostream>
#include <optional>
#include <string>

struct Cli {
    std::string config_path{"config/benchmark_config.json"};
    std::optional<std::size_t> core_threads;
    std::optional<std::size_t> duration_seconds;
    std::optional<bool>        duration_mode; // true: time, false: tasks
    std::optional<std::size_t> total_tasks;
};

static Cli parse_cli(int argc, char** argv) {
    Cli cli{};
    int idx = 1;
    if (idx < argc && std::string(argv[idx]) == "--config") {
        if (idx + 1 < argc) cli.config_path = argv[idx + 1];
        idx += 2;
    }
    if (idx < argc) { cli.core_threads = static_cast<std::size_t>(std::stoul(argv[idx++])); }
    if (idx < argc) { cli.duration_seconds = static_cast<std::size_t>(std::stoul(argv[idx++])); }
    if (idx < argc) {
        std::string mode = argv[idx++];
    if (mode == "tasks") cli.duration_mode = false; // by tasks
    else                  cli.duration_mode = true;  // by time (default)
    }
    if (idx < argc) { cli.total_tasks = static_cast<std::size_t>(std::stoul(argv[idx++])); }
    return cli;
}

int main(int argc, char** argv) {
    // Initialize logger (set level to warn in benchmark to reduce noise)
    thread_pool::log::InitializeLogger("config/logger_config.json");
    thread_pool::log::SetLevel("warn");

    auto cli = parse_cli(argc, argv);

    // Read raw JSON to support multiple 'scenarios' test cases
    nlohmann::json jroot;
    try {
        std::ifstream ifs(cli.config_path);
        if (ifs.is_open()) {
            ifs >> jroot;
        }
    } catch (...) {
        // Ignore and proceed with single config
    }

    // Base config (defaults + config file)
    auto base_cfg = bench_tp::BenchmarkConfig::LoadFromFile(cli.config_path);

    auto apply_override = [](bench_tp::BenchmarkConfig& cfg, const nlohmann::json& j) {
        if (j.contains("thread_pool")) {
            const auto& p = j["thread_pool"];
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
            const auto& b = j["benchmark"];
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
    };

    const bool has_positional_override = cli.core_threads.has_value() || cli.duration_seconds.has_value() || cli.duration_mode.has_value() || cli.total_tasks.has_value();

    // If JSON includes scenarios and no positional overrides are provided, run all scenarios
    if (!has_positional_override && jroot.is_object() && jroot.contains("scenarios") && jroot["scenarios"].is_array()) {
        const auto& arr = jroot["scenarios"];
        std::size_t idx = 0;
        for (const auto& sc : arr) {
            auto cfg = base_cfg; // inherit base config
            apply_override(cfg, sc);
            std::string name;
            if (sc.contains("name")) {
                try { name = sc["name"].get<std::string>(); } catch (...) {}
            }
            if (name.empty()) name = std::string("Scenario-") + std::to_string(++idx);

            std::cout << "\n" << std::string(50, '=') << "\n";
            std::cout << "Running scenario: " << name << std::endl;
            std::cout << std::string(50, '=') << std::endl;
            // Control log level per scenario: suppress to 'error' when enable_logging=false
            thread_pool::log::SetLevel(cfg.enable_logging ? "warn" : "error");
            bench_tp::ThreadPoolBenchmark bench(cfg);
            auto result = bench.RunBenchmark();
            bench.PrintResult(result);
        }
    } else {
    // Single run: support positional overrides
        auto cfg = base_cfg;
        if (cli.core_threads) cfg.core_threads = *cli.core_threads;
        if (cli.duration_seconds) cfg.duration_seconds = *cli.duration_seconds;
        if (cli.duration_mode) cfg.use_duration_mode = *cli.duration_mode;
        if (cli.total_tasks) cfg.total_tasks = *cli.total_tasks;

    // Control log level per config
        thread_pool::log::SetLevel(cfg.enable_logging ? "warn" : "error");
        bench_tp::ThreadPoolBenchmark bench(cfg);
        auto result = bench.RunBenchmark();
        bench.PrintResult(result);
    }
    return 0;
}