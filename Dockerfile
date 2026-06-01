# Expects build artifacts to be present in the build context, produced by
# scripts/build.docker.sh (which builds via docker run with host /nix mounted):
#   out/kds-closure.tar.gz  — runtime Nix closure
#   out/kds                 — compiled server binary

FROM alpine:3.20

WORKDIR /app

# Unpack the nix closure at the same /nix/store/... paths the binary references.
# GNU tar is required: BusyBox tar does not support -P (preserve absolute paths).
COPY out/kds-closure.tar.gz /tmp/
RUN apk add --no-cache tar && tar -xzPf /tmp/kds-closure.tar.gz && rm /tmp/kds-closure.tar.gz

COPY out/kds /app/kds

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
