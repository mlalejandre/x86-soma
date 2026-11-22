FROM debian:bookworm

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        gcc \
        g++ \
        binutils \
        make \
        python3 \
        grub-pc-bin \
        grub-efi-amd64-bin \
        grub-common \
        xorriso \
        mtools \
        gcc-aarch64-linux-gnu \
        binutils-aarch64-linux-gnu && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /soma
