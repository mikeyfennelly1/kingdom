# Stage 1: Build
FROM nixos/nix:2.31.5 AS builder

# Set up the working directory
WORKDIR /app

# Copy nix configuration first to leverage Docker layer caching
COPY config/build.shell.nix ./config/

# Pre-fetch and realize the build environment
RUN nix-shell ./config/build.shell.nix --run "true"

# Copy the rest of the source code
COPY . .

# Build the project using the build script
# This script uses nix-shell ./config/build.shell.nix internally
RUN ./scripts/build.sh

# Stage 2: Runtime
FROM nixos/nix:2.31.5

# Set up the working directory
WORKDIR /app

# Copy nix configuration
COPY config/build.shell.nix ./config/

# Realize the environment so all dependencies (libraries) are in the Nix store
RUN nix-shell ./config/build.shell.nix --run "true"

# Copy binaries from the builder stage
COPY --from=builder /app/build/kds/kds /app/kds
COPY --from=builder /app/build/kdctl/kdctl /app/kdctl

# Expose the default server port
EXPOSE 8080

# Environment variables
ENV KD_LOG_LEVEL=info
ENV KD_PORT=8080

# Run the server using nix-shell to ensure the environment (libraries) is set up
ENTRYPOINT ["nix-shell", "./config/build.shell.nix", "--run", "/app/kds"]
