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

# Copy binaries and shared library from the builder stage
COPY --from=builder /app/build/kds/kds /app/kds
COPY --from=builder /app/build/kdctl/kdctl /app/kdctl
COPY --from=builder /app/build/libkd/libkd.so.1 /app/libkd.so.1

# Expose the default server port
EXPOSE 8080

# Build-time parameters — no defaults, must be supplied via --build-arg
ARG POSTGRES_USER
ARG POSTGRES_PASSWORD
ARG POSTGRES_DB
ARG POSTGRES_HOST
ARG POSTGRES_PORT
ARG KD_TLS_CERT
ARG KD_TLS_KEY
ARG KD_JWT_TTL_SECONDS
ARG KD_LOG_LEVEL
ARG KD_PORT

# Promote to runtime environment
ENV POSTGRES_USER=${POSTGRES_USER}
ENV POSTGRES_PASSWORD=${POSTGRES_PASSWORD}
ENV POSTGRES_DB=${POSTGRES_DB}
ENV POSTGRES_HOST=${POSTGRES_HOST}
ENV POSTGRES_PORT=${POSTGRES_PORT}
ENV KD_DB_URL=postgresql://${POSTGRES_USER}:${POSTGRES_PASSWORD}@${POSTGRES_HOST}:${POSTGRES_PORT}/${POSTGRES_DB}
ENV KD_TLS_CERT=${KD_TLS_CERT}
ENV KD_TLS_KEY=${KD_TLS_KEY}
ENV KD_JWT_TTL_SECONDS=${KD_JWT_TTL_SECONDS}
ENV KD_LOG_LEVEL=${KD_LOG_LEVEL}
ENV KD_PORT=${KD_PORT}

# Run the server using nix-shell to ensure the environment (libraries) is set up
ENTRYPOINT ["nix-shell", "./config/build.shell.nix", "--run", "LD_LIBRARY_PATH=/app:$LD_LIBRARY_PATH /app/kds"]
