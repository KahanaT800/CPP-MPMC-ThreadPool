#!/bin/bash
set -e

rm -rf build
if [ ! -d build ]; then
  mkdir build
fi
cd build

if [ -z "$VCPKG_ROOT" ]; then
  echo "Please set the VCPKG_ROOT environment variable"
  exit 1
fi
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

cmake --build . -j$(nproc)
