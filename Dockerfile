FROM nixos/nix:2.34.7 AS builder

RUN echo 'experimental-features = nix-command flakes' >> /etc/nix/nix.conf && \
    echo 'sandbox = false' >> /etc/nix/nix.conf

WORKDIR /app
COPY . .
RUN nix develop .#kds --command bash ./scripts/build.sh

# ── Runtime ───────────────────────────────────────────────────────────────────

FROM alpine:3.20

WORKDIR /app

# Unpack the nix closure at the same /nix/store/... paths the binary references.
# GNU tar is required: BusyBox tar does not support -P (preserve absolute paths).
RUN --mount=type=bind,source=out,target=/mnt/out \
    stat /mnt/out/kds-closure.tar.gz > /dev/null 2>&1 \
    || { printf "\nERROR: out/kds-closure.tar.gz not found.\nRun scripts/create-closure.sh to generate it before building this image.\n\n" >&2; exit 1; }
COPY out/kds-closure.tar.gz /tmp/
RUN apk add --no-cache tar && tar -xzPf /tmp/kds-closure.tar.gz && rm /tmp/kds-closure.tar.gz

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
