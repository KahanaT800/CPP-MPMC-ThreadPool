# syntax=docker/dockerfile:1.7

# Dev/build stage — includes all build tools and dev headers
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       cmake \
       ninja-build \
       git \
       pkg-config \
       # dependencies
       libspdlog-dev \
       nlohmann-json3-dev \
       libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy only manifests and scripts first to leverage Docker layer cache
COPY CMakeLists.txt .
COPY include ./include
COPY src ./src
COPY bench ./bench
COPY test ./test
COPY config ./config
COPY scripts ./scripts
COPY README.md ./README.md

# Configure & build (Release by default). You can override with --build-arg BUILD_TYPE=Debug
ARG BUILD_TYPE=Release
RUN cmake -S . -B /app/build -G Ninja -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    && cmake --build /app/build --target all -- -v

# Optional: run tests at build time (can be disabled via build arg)
ARG RUN_TESTS=ON
RUN if [ "$RUN_TESTS" = "ON" ]; then \
      ctest --test-dir /app/build --output-on-failure; \
    else \
      echo "Skipping tests at build time"; \
    fi

# Runtime stage — minimal image that contains built binaries and configs
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Only runtime needs spdlog shared (if any) and basic libs; json is header-only
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       libspdlog1 \
       ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Bring binaries and configs from build stage
COPY --from=build /app/build /app/build
COPY --from=build /app/config /app/config
COPY --from=build /app/scripts /app/scripts

# Default command: open a shell. See README for how to run tests/bench
CMD ["/bin/bash"]
