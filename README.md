# Thread Pool (MPMC)

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

Your numbers will vary, but here’s a snapshot from a modern x86 workstation using the built-in scenarios:

| Scenario | Throughput (tasks/s) | Peak Threads | Policy |
|----------|----------------------|--------------|--------|
| Duration-2core-2s | ~1.6M | 6 | BLOCK |
| Duration-8core-5s | ~1.3M | 16 | BLOCK |
| TaskCount-4core-1M | ~95K | 16 | BLOCK |
| OverwritePolicy-Test | ~1.26M | 4 | OVERWRITE |
| CPU-Bottleneck-2core | ~11K | 2 | BLOCK |
| Queue-Bottleneck-BLOCK | ~1.93M | 4 | BLOCK |
| DiscardPolicy-Test | ~1.32M | 2 | DISCARD |
| IO-Sleep-Workload | ~3.8K | 16 | BLOCK |
| Dynamic-Scaling-Test | ~55K | 16 | BLOCK |
| Multi-Submitter-TaskMode | ~45K | 8 | BLOCK |
| Low-Concurrency | ~1.48M | 4 | DISCARD |
| Medium-Concurrency | ~1.17M | 8 | DISCARD |
| High-Concurrency | ~1.27M | 16 | DISCARD |
| VeryHigh-Concurrency | ~1.32M | 25 | DISCARD |
| Baseline-50M-Tasks | ~764K | 32 | DISCARD |

A few takeaways:

- Lightweight tasks hit 1.3M–1.9M tasks/s depending on cores and policy
- 200µs CPU work drops to ~11K tasks/s (as expected)
- Dynamic scaling grows from 2 → 16 workers under load and stays stable
- `BLOCK` maximizes throughput, `DISCARD`/`OVERWRITE` control pressure (and tell you how often)
- IO/sleep workloads are bound by your sleep time (e.g., 2ms → ~3.8K tasks/s)

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
