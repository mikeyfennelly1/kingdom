# Stage 1: Build
# Runs inside a Nix-enabled image. Delegates entirely to the project build
# scripts — no inline cmake/nix commands here.
FROM nixos/nix:2.34.7 AS builder

WORKDIR /app
COPY . .

# Configure nix.conf (enables flakes, disables sandbox for Docker).
RUN bash ./scripts/configure-nix-host.sh

# Build the binary and assemble the nix store closure.
# create-closure.sh calls build.sh internally, then tars the closure to
# out/kds-closure.tar.gz.
RUN bash ./scripts/create-closure.sh

# Stage 2: Minimal Alpine runtime
FROM alpine:3.20

# Unpack the nix closure at the same /nix/store/... paths the binary references.
COPY --from=builder /app/out/kds-closure.tar.gz /tmp/
RUN tar -xzPf /tmp/kds-closure.tar.gz && rm /tmp/kds-closure.tar.gz

COPY --from=builder /app/build/kds/kds /app/kds

EXPOSE 8080

ARG POSTGRES_USER
ARG POSTGRES_DB
ARG POSTGRES_HOST
ARG POSTGRES_PORT
ARG KD_TLS_CERT
ARG KD_JWT_TTL_SECONDS
ARG KD_LOG_LEVEL
ARG KD_PORT

ENV POSTGRES_USER=${POSTGRES_USER} \
    POSTGRES_DB=${POSTGRES_DB} \
    POSTGRES_HOST=${POSTGRES_HOST} \
    POSTGRES_PORT=${POSTGRES_PORT} \
    KD_TLS_CERT=${KD_TLS_CERT} \
    KD_JWT_TTL_SECONDS=${KD_JWT_TTL_SECONDS} \
    KD_LOG_LEVEL=${KD_LOG_LEVEL} \
    KD_PORT=${KD_PORT}

ENTRYPOINT ["/app/kds"]
