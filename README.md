# Thread Pool (MPMC)

[![CI](https://github.com/KahanaT800/CPP-MPMC-ThreadPool/actions/workflows/ci.yml/badge.svg)](https://github.com/KahanaT800/CPP-MPMC-ThreadPool/actions/workflows/ci.yml)

Fast, friendly, and ready for real workloads — a modern C++17 thread pool with a lock-free MPMC queue, smart scaling, and handy stats. Comes with unit tests, stress tests, and a configurable benchmark so you can see how it behaves on your machine.
This repository serves as a learning-oriented implementation for exploring design trade-offs, benchmarking, and sharing ideas.

## Features ✨

- Dynamic scaling: grows and shrinks workers based on load
- Backpressure policies: `BLOCK`, `DISCARD`, or `OVERWRITE` when the queue is full
- Lock-free MPMC queue: bounded ring buffer with batch ops and fast paths
- Rich stats: `ThreadPool::Statistics` + optional real-time monitoring (`spdlog`)
- Turnkey benchmarks: multiple scenarios in `config/benchmark_config.json`

## Quick Start 🚀

### Prerequisites

- C++17 compiler (GCC / Clang / MSVC)
- Libraries: `spdlog`, `nlohmann_json`
- Tests: `GTest`

### Build

```bash
./scripts/build_test.sh
```

### Run tests

```bash
./scripts/start_test.sh
```

### Run benchmarks

```bash
./scripts/run_benchmark.sh
```

Want to override defaults? Pass args after `--`:

```bash
# core_threads duration mode total_tasks
./scripts/run_benchmark.sh -- --config config/benchmark_config.json 4 10 time
```

Scenarios live in `config/benchmark_config.json`. See below for how they’re structured.

## Using the pool

```cpp
#include "thread_pool/thread_pool.hpp"

thread_pool::ThreadPool pool(4, 1024); // 4 core threads, queue cap 1024
pool.Start();

auto fut = pool.Submit([] { return 42; });
int answer = fut.get();

pool.Post([] { /* fire-and-forget */ });

auto stats = pool.GetStatistics();
pool.Stop(thread_pool::StopMode::Graceful);
```

### Handy knobs

- `queue_full_policy`: what to do when the queue is full
- `enable_dynamic_threads` + thresholds: auto scale workers
- `pending_hi/pending_low`, `debounce_hits`, `cooldown_ms`: scaling sensitivity
- `keep_alive_time_ms`: idle thread lifetime

## Benchmark config

Three layers in `config/benchmark_config.json`:

1. `thread_pool`: defaults for the pool
2. `benchmark`: defaults for the benchmark
3. `scenarios`: mix-and-match overrides with a friendly name

Example:

```json
{
  "scenarios": [
    {
      "name": "CPU-Bottleneck-2core",
      "thread_pool": { "core_threads": 2, "max_threads": 2 },
      "benchmark": { "use_duration_mode": true, "duration_seconds": 5, "task_work_us": 200 }
    }
  ]
}
```

The benchmark prints throughput, queue usage, discarded/overwritten counts, and per-thread numbers. Toggle live output with `enable_real_time_monitoring`/`monitoring_interval_ms`.

## What performance looks like (on one box)

| Scenario                 | Throughput (tasks/s) | Peak Threads | Queue Util.  | Discard/Overwrite | Policy |
|--------------------------|----------------------|--------------|--------------|-------------------|--------|
| Duration-2core-2s        | 3,043,678            | 6            | 99.9%        | 0                 | BLOCK  |
| OverwritePolicy-Test     | 1,577,654            | 4            | 99.9%        | 918,514 overwritten| OVERWRITE |
| DiscardPolicy-Test       | 1,776,014            | 2            | 100.0%       | 4,987,230 discarded| DISCARD |
| CPU-Bottleneck-2core     | 11,225               | 2            | 100.0%       | 0                 | BLOCK  |
| IO-Sleep-Workload        | 3,772                | 16           | 100.0%       | 0                 | BLOCK  |
| Dynamic-Scaling-Test     | 77,197               | 16           | 100.0%       | 0                 | BLOCK  |
| VeryHigh-Concurrency     | 1,512,089            | 25           | 104.9%       | 1,525,989 discarded| DISCARD |
| Baseline-50M-Tasks       | 916,418              | 32           | 167.8%       | 31,601,177 discarded| DISCARD |

Key takeaways:

- Lightweight tasks: throughput up to 1.8M–3M tasks/s, queue utilization is extremely high
- High concurrency/large task scenarios: discard/overwrite policies effectively prevent queue overflow
- CPU-bound scenarios (e.g. 200µs work): throughput drops to ~11K tasks/s, as expected
- Dynamic scaling: thread pool automatically expands from 2 to 16 workers, maintaining high throughput
- IO/Sleep scenarios: throughput is limited by task duration, much lower

## Logging

Configured via `config/logger_config.json` (powered by `spdlog`). Benchmarks auto-tune log level per scenario to keep the output readable.

## Project layout

- `include/` public headers (pool, MPMC queue, etc.)
- `src/` library code (pool, logger, config)
- `bench/` benchmark framework + main
- `test/` GoogleTest unit/stress tests
- `config/` logger + benchmark configs
- `scripts/` helpers for build/run

## License

MIT — see LICENSE.

---

Questions, ideas, or cool results to share? PRs and issues welcome 🙌
