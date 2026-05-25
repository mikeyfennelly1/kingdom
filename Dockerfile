# Stage 1: Build
FROM ubuntu:24.04 AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    libssl-dev \
    libpq-dev \
    libpqxx-dev \
    pkg-config \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy the entire project
COPY . .

# Build the project
RUN cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build

# Stage 2: Runtime
FROM ubuntu:24.04

# Install runtime dependencies (OpenSSL, PostgreSQL)
RUN apt-get update && apt-get install -y \
    libssl3 \
    libpq5 \
    libpqxx-7.0t64 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy binaries from the builder stage
COPY --from=builder /app/build/kds/kds /app/kds
COPY --from=builder /app/build/kdctl/kdctl /app/kdctl

# Expose the default server port
EXPOSE 8080

# Environment variables
ENV KD_LOG_LEVEL=info
ENV KD_PORT=8080

# Run the server by default
ENTRYPOINT ["/app/kds"]
