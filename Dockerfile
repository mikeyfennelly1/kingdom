FROM alpine:3.20 AS builder

RUN apk add --no-cache bash curl tar xz && \
    curl -fsSL https://install.determinate.systems/nix | bash -s -- install linux \
        --init none \
        --extra-conf "sandbox = false" \
        --no-confirm && \
    echo 'experimental-features = nix-command flakes' >> /etc/nix/nix.conf

ENV PATH="/nix/var/nix/profiles/default/bin:${PATH}"

WORKDIR /app
COPY . .

# Unpack the nix closure at the same /nix/store/... paths the binary references.
# GNU tar is required: BusyBox tar does not support -P (preserve absolute paths).
COPY scripts/unpack-store.sh /app/unpack-store.sh
COPY scripts/unpack-closure.sh /app/unpack-closure.sh
RUN --mount=type=bind,source=out,target=/mnt/out \
    bash -o pipefail -c 'bash /app/unpack-store.sh 2>&1 | tee /app/unpack-store.log'

RUN nix develop .#kds --command bash ./scripts/build.sh

# ── Runtime ───────────────────────────────────────────────────────────────────

FROM scratch AS logs
COPY --from=builder /app/unpack-store.log /

FROM alpine:3.20

WORKDIR /app

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
