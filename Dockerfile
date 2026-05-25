# Stage 1: Build
FROM jetpackio/devbox:0.17.0 AS builder

# Set up the working directory
WORKDIR /app
USER root:root
RUN mkdir -p /app && chown ${DEVBOX_USER}:${DEVBOX_USER} /app
USER ${DEVBOX_USER}:${DEVBOX_USER}

# Copy only devbox files first to leverage Docker layer caching
COPY --chown=${DEVBOX_USER}:${DEVBOX_USER} devbox.json devbox.lock ./

# Install the environment defined in devbox.json
# Using DEVBOX_DEBUG=1 for more info and NIX_CONFIG to increase timeouts
RUN DEVBOX_DEBUG=1 NIX_CONFIG="connect-timeout = 30" devbox install

# Copy the rest of the source code
COPY --chown=${DEVBOX_USER}:${DEVBOX_USER} . .

# Build the project using the devbox environment
# We use 'devbox run' to ensure the environment (compilers, libs) is active
RUN devbox run cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release
RUN devbox run cmake --build build

# Stage 2: Runtime
FROM jetpackio/devbox:0.17.0

# Set up the working directory
WORKDIR /app
USER root:root
RUN mkdir -p /app && chown ${DEVBOX_USER}:${DEVBOX_USER} /app
USER ${DEVBOX_USER}:${DEVBOX_USER}

# Copy devbox files and install runtime environment
COPY --chown=${DEVBOX_USER}:${DEVBOX_USER} devbox.json devbox.lock ./
RUN devbox install

# Copy binaries from the builder stage
COPY --from=builder /app/build/kds/kds /app/kds
COPY --from=builder /app/build/kdctl/kdctl /app/kdctl

# Expose the default server port
EXPOSE 8080

# Environment variables
ENV KD_LOG_LEVEL=info
ENV KD_PORT=8080

# Run the server using devbox run to ensure environment is set up
ENTRYPOINT ["devbox", "run", "/app/kds"]
