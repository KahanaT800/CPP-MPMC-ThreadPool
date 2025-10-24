#!/bin/bash
set -e

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
  echo "Usage: ./scripts/start_test.sh [options]"
  echo "Options:"
  echo "  -R <regex>              Run tests matching regex"
  echo "  -j <N>                  Run tests in parallel"
  echo "  --list                  List available tests"
  echo "  --verbose               Show detailed output"
  echo "  --output-on-failure     Show output of failed tests (default)"
  exit 0
fi

if [ ! -d "build" ]; then
  echo "build/ directory not found. Please run ./scripts/build_test.sh first."
  exit 1
fi

CTEST_ARGS="--output-on-failure"

while [[ $# -gt 0 ]]; do
  case $1 in
    -R)
      CTEST_ARGS="$CTEST_ARGS -R $2"
      shift 2
      ;;
    -j)
      CTEST_ARGS="$CTEST_ARGS -j $2"
      shift 2
      ;;
    --list)
      CTEST_ARGS="$CTEST_ARGS -N"
      shift
      ;;
    --verbose)
      CTEST_ARGS="$CTEST_ARGS -VV"
      shift
      ;;
    *)
      shift
      ;;
  esac
done

cd build
ctest $CTEST_ARGS

