#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   scripts/run_benchmark.sh [-- build-dir] [-- args for benchmark...]
# Examples:
#   scripts/run_benchmark.sh
#   scripts/run_benchmark.sh -- --config config/benchmark_config.json 4 10 time
#   bash scripts/run_benchmark.sh -- --config config/benchmark_config.json 2 0 tasks 500000

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

# Allow custom build dir via env BUILD_DIR or first param like BUILD_DIR=build-release
if [[ ${1:-} == BUILD_DIR=* ]]; then
  BUILD_DIR="${ROOT_DIR}/${1#BUILD_DIR=}"
  shift || true
fi

# Split args: everything after a standalone -- goes to the benchmark binary
BENCH_ARGS=()
if [[ ${1:-} == "--" ]]; then
  shift
  BENCH_ARGS=("$@")
fi

# Configure & build
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" -j

# Run benchmark (default args if none passed)
BIN="${BUILD_DIR}/bench/thread_pool_benchmark"
if [[ ! -x "${BIN}" ]]; then
  echo "Error: benchmark binary not found at ${BIN}" >&2
  exit 1
fi

if [[ ${#BENCH_ARGS[@]} -eq 0 ]]; then
  BENCH_ARGS=(--config config/benchmark_config.json)
fi

"${BIN}" "${BENCH_ARGS[@]}"