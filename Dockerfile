# Stage 1: Build
FROM nixos/nix:2.31.5 AS builder

WORKDIR /app

COPY config/build.shell.nix ./config/
RUN nix-shell ./config/build.shell.nix --run "true"

COPY . .
RUN ./scripts/build.sh

# Collect the full runtime closure for kds.
# The binary's RPATH and PT_INTERP both reference absolute /nix/store/... paths,
# so those exact paths must exist in the runtime image.
RUN ldd /app/build/kds/kds \
      | awk '$2=="=>" && $3~/^\/nix\/store/ { n=split($3,a,"/"); print "/nix/store/"a[4] }' \
      | sort -u \
      | xargs nix-store --query --requisites \
      | sort -u \
      | tar -czPf /tmp/kds-closure.tar.gz --files-from=-

# Stage 2: Minimal Alpine runtime
FROM alpine:3.20

# Unpack the nix closure at the same /nix/store/... paths the binary references
COPY --from=builder /tmp/kds-closure.tar.gz /tmp/
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
