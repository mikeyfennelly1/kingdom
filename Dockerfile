FROM alpine:3.20 AS builder

RUN apk add --no-cache bash curl tar xz && \
    curl -fsSL https://install.determinate.systems/nix | bash -s -- install linux \
        --init none \
        --extra-conf "sandbox = false" \
        --no-confirm && \
    echo 'experimental-features = nix-command flakes' >> /etc/nix/nix.conf

ENV PATH="/nix/var/nix/profiles/default/bin:${PATH}"

WORKDIR /app

# Unpack the nix closure at the same /nix/store/... paths the binary references.
# GNU tar is required: BusyBox tar does not support -P (preserve absolute paths)
COPY ./out/kds-closure.tar.gz /app/out/kds-closure.tar.gz
COPY ./out/kds-toolchain.tar.gz /app/out/kds-toolchain.tar.gz
COPY ./scripts/unpack-store.sh /app/scripts/unpack-store.sh
COPY ./scripts/move-store.sh ./scripts/move-store.sh
COPY ./scripts/build.sh ./scripts/build.sh

RUN bash -o pipefail -c 'bash /app/scripts/unpack-store.sh 2>&1'
RUN tar -xf /app/out/kds-toolchain.tar.gz -C /app/unpacked-store
RUN bash -o pipefail -c 'bash /app/scripts/move-store.sh 2>&1'


ENTRYPOINT ["/bin/bash"]
