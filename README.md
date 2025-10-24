# Thread Pool (MPMC)

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![CI Status](https://github.com/KahanaT800/CPP-MPMC-ThreadPool/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/KahanaT800/CPP-MPMC-ThreadPool/actions/workflows/ci.yml)
[![Release](https://github.com/KahanaT800/CPP-MPMC-ThreadPool/actions/workflows/cd.yml/badge.svg)](https://github.com/KahanaT800/CPP-MPMC-ThreadPool/actions/workflows/cd.yml)
[![Docker](https://img.shields.io/badge/Docker-Ready-2496ED?style=flat&logo=docker&logoColor=white)](https://ghcr.io/kahanat800/cpp-mpmc-threadpool)

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

## Docker 🐳

Build multi-stage images (build and runtime) and run tests/benchmarks inside containers.

- Build the development/build image (includes compilers, CMake, and test run):

```bash
docker build -t threadpool:build --target build .
```

- Build the minimal runtime image (only binaries + configs):

```bash
docker build -t threadpool:runtime .
```

- Run tests (from the build image):

```bash
docker run --rm -it threadpool:build ctest --test-dir /app/build --output-on-failure
```

- Run benchmarks (from the runtime image; use the prebuilt binary):

```bash
# Quick 2-core performance test
docker run --rm -it threadpool:runtime /app/build/bench/thread_pool_benchmark 2 2 time

# Full benchmark suite with config
docker run --rm -it threadpool:runtime /app/build/bench/thread_pool_benchmark --config /app/config/benchmark_config.json
```

Expected performance: 2.3M+ tasks/s with 82ns average latency.

Optional build args:

- `--build-arg BUILD_TYPE=Debug` (default: Release)
- `--build-arg RUN_TESTS=OFF` to skip running tests during the build stage

### Prebuilt image (GHCR) 📦

You can also pull a prebuilt runtime image from GitHub Container Registry once a release tag is published:

```bash
# pull a specific version (recommended)
docker pull ghcr.io/kahanat800/cpp-mpmc-threadpool:v1.0.0

# or always track latest
docker pull ghcr.io/kahanat800/cpp-mpmc-threadpool:latest

# run benchmark from the prebuilt runtime image
docker run --rm -it ghcr.io/kahanat800/cpp-mpmc-threadpool:v1.0.0 \
  /app/build/bench/thread_pool_benchmark 2 2 time

# Expected output: ~2.3M tasks/s throughput, 82ns avg task time
```

Image tags follow the repository tags (e.g., `v1.0.0`) and also publish `:latest`.

## Using the pool 💻

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

## Benchmark config 📊

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

## Performance Benchmarks 📊

Real-world performance results from comprehensive benchmarking scenarios:

| Scenario                 | Throughput (tasks/s) | Peak Threads | Queue Util.  | Discard/Overwrite | Avg Task Time |
|--------------------------|----------------------|--------------|--------------|-------------------|---------------|
| Duration-2core-2s        | 2,303,564            | 6            | 99.98%       | 0                 | 82 ns         |
| Duration-8core-5s        | 1,950,055            | 16           | 100.00%      | 0                 | 99 ns         |
| OverwritePolicy-Test     | 1,921,085            | 4            | 99.98%       | 1,553,819 overwritten | 89 ns   |
| DiscardPolicy-Test       | 1,899,733            | 2            | 100.00%      | 7,164,039 discarded | 73 ns     |
| Low-Concurrency          | 1,507,409            | 4            | 131.07%      | 8,335,238 discarded | 80 ns     |
| Medium-Concurrency       | 1,435,485            | 8            | 131.07%      | 9,636,690 discarded | 88 ns     |
| High-Concurrency         | 732,511              | 14           | 131.07%      | 2,011,617 discarded | 104 ns    |
| Multi-Submitter-TaskMode | 83,209               | 8            | 100.00%      | 0                 | 156 ns        |

### Performance Insights 🎯

**🚀 Excellent Performance (All scenarios > 100K tasks/s)**
- Peak throughput: **2.3M tasks/s** with 2-core configuration
- Ultra-low latency: **73-156 ns** average task execution time
- Efficient scaling: Up to 16 threads with intelligent load balancing

**⚡ Key Findings:**
- **Lightweight tasks**: Consistent 1.4M-2.3M tasks/s throughput across scenarios
- **Queue policies**: Overwrite and Discard policies handle extreme loads gracefully
- **Dynamic scaling**: Automatic thread adjustment from 2 to 16 workers based on load
- **Memory efficiency**: Queue utilization up to 131% with smart overflow handling
- **Per-thread efficiency**: 52K-950K tasks/s per thread depending on concurrency level

**🎚️ Scaling Characteristics:**
- Low concurrency (2-4 threads): 1.5M+ tasks/s with excellent per-thread efficiency
- Medium concurrency (4-8 threads): 1.4M+ tasks/s with balanced load distribution  
- High concurrency (8-16 threads): 732K+ tasks/s, optimal for CPU-intensive workloads
- Task-based mode: 83K tasks/s with precise completion guarantees

### Configuration Recommendations 🔧

Based on benchmark results, here are optimal configurations for different use cases:

**🏃‍♂️ Maximum Throughput (Lightweight Tasks):**
```json
{
  "core_threads": 2,
  "max_threads": 6, 
  "queue_cap": 32768,
  "queue_policy": "Block"
}
```
*Expected: 2.3M+ tasks/s, 82ns avg latency*

**⚖️ Balanced Load (Mixed Workloads):**
```json
{
  "core_threads": 4,
  "max_threads": 8,
  "queue_cap": 100000,
  "queue_policy": "Block"  
}
```
*Expected: 1.4M+ tasks/s, 88ns avg latency*

**🛡️ High Resilience (Overflow Protection):**
```json
{
  "core_threads": 2,
  "max_threads": 4,
  "queue_cap": 8192,
  "queue_policy": "Overwrite"
}
```
*Expected: 1.9M+ tasks/s with automatic overflow handling*

### Understanding Benchmark Metrics 📈

**Throughput**: Tasks completed per second - higher is better
- 🟢 Excellent: >1M tasks/s  
- 🟡 Good: >100K tasks/s
- 🔴 Needs optimization: <50K tasks/s

**Queue Utilization**: Percentage of queue capacity used
- 100%+: High load, consider increasing capacity
- 90-99%: Optimal utilization 
- <50%: Underutilized, can reduce capacity

**Discard/Overwrite**: Tasks dropped due to queue overflow
- Use Discard policy for strict memory bounds
- Use Overwrite policy for latest-data scenarios
- Use Block policy for guaranteed processing

**Average Task Time**: Nanoseconds per task execution
- <100ns: Excellent for lightweight operations
- 100-1000ns: Good for moderate complexity
- >1000ns: Consider task batching or optimization

## Logging 📝

Configured via `config/logger_config.json` (powered by `spdlog`). Benchmarks auto-tune log level per scenario to keep the output readable.

## Project layout 📁

- `include/` public headers (pool, MPMC queue, etc.)
- `src/` library code (pool, logger, config)
- `bench/` benchmark framework + main
- `test/` GoogleTest unit/stress tests
- `config/` logger + benchmark configs
- `scripts/` helpers for build/run

## License 📄

MIT — see LICENSE.

---

Questions, ideas, or cool results to share? PRs and issues welcome 🙌
