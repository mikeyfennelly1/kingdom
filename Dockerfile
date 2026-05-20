# Multi-stage Dockerfile for Kingdom Server (kds)
FROM ubuntu:24.04 AS builder

# Prevent interactive prompts during package installation
ARG DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libpqxx-dev \
    libssl-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy the entire project
COPY . .

# Build the project
RUN cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build --target kds

# Final stage: runtime image
FROM ubuntu:24.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libpqxx-6.4 \
    libssl3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the binary from the builder stage
COPY --from=builder /app/build/kds/kds .

# Expose port 8080 (default for kds)
EXPOSE 8080

# Run the server
CMD ["./kds"]
