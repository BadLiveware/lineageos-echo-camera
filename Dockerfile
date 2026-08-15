# syntax=docker/dockerfile:1.7@sha256:a57df69d0ea827fb7266491f2813635de6f17269be881f696fbfdf2d83dda33e

FROM --platform=linux/amd64 ubuntu:20.04@sha256:c664f8f86ed5a386b0a340d981b8f81714e21a8b9c73f658c4bea56aa179d54a AS build-env

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_SNAPSHOT=20250404T000000Z
# The minimal base has no CA bundle. APT authenticates the signed snapshot
# metadata while bootstrapping ca-certificates, then restores TLS validation.
RUN printf '%s\n' \
        "deb [check-valid-until=no] https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT} focal main restricted universe multiverse" \
        "deb [check-valid-until=no] https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT} focal-updates main restricted universe multiverse" \
        "deb [check-valid-until=no] https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT} focal-security main restricted universe multiverse" \
        > /etc/apt/sources.list \
    && apt-get -o Acquire::https::Verify-Peer=false update \
    && apt-get -o Acquire::https::Verify-Peer=false install -y --no-install-recommends ca-certificates \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        bc \
        bison \
        build-essential \
        ccache \
        curl \
        file \
        flex \
        g++-multilib \
        gcc-multilib \
        git \
        git-lfs \
        gnupg \
        gperf \
        imagemagick \
        lib32readline-dev \
        lib32z1-dev \
        libelf-dev \
        liblz4-tool \
        libncurses5 \
        libncurses5-dev \
        libsdl1.2-dev \
        libssl-dev \
        libxml2 \
        libxml2-utils \
        lzop \
        openssh-client \
        pngcrush \
        python-is-python3 \
        python3 \
        rsync \
        schedtool \
        squashfs-tools \
        unzip \
        xsltproc \
        xz-utils \
        zip \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

ARG REPO_VERSION=2.54
ARG REPO_SHA256=6cba294d6218bbd4a1500598207b3979c752c7a122aef9429e4d7fef688833b5
# Soong's sandbox can read /etc/mtab but does not mount /proc, so replace
# the standard /proc/mounts symlink with an empty offline mount table.
RUN curl -fsSL "https://storage.googleapis.com/git-repo-downloads/repo-${REPO_VERSION}" -o /usr/local/bin/repo \
    && echo "${REPO_SHA256}  /usr/local/bin/repo" | sha256sum -c - \
    && chmod 0755 /usr/local/bin/repo \
    && rm -f /etc/mtab \
    && touch /etc/mtab \
    && ldconfig -p | grep -q 'libncurses.so.5' \
    && ldconfig -p | grep -q 'libtinfo.so.5'

FROM build-env AS build-env-check
RUN test -f /etc/mtab \
    && test ! -L /etc/mtab \
    && test ! -s /etc/mtab \
    && command -v ssh >/dev/null

FROM build-env-check AS builder
WORKDIR /opt/lineageos-camera
COPY manifests/ manifests/
COPY patches/ patches/
COPY scripts/apply-amazon-patches.sh scripts/apply-patches.sh scripts/verify-proprietary-files.sh scripts/build-checkers-container.sh scripts/

ARG LINEAGE_CACHE_ID=lineageos-checkers
RUN --mount=type=cache,id=${LINEAGE_CACHE_ID},target=/workspace,sharing=locked \
    --mount=type=bind,from=firmware,target=/firmware,readonly \
    /opt/lineageos-camera/scripts/build-checkers-container.sh \
        /workspace/lineage-18.1 \
        /firmware \
        /artifacts

FROM build-env-check AS crown-builder
WORKDIR /opt/lineageos-camera
COPY manifests/ manifests/
COPY patches/ patches/
COPY scripts/apply-amazon-patches.sh \
     scripts/apply-patches.sh \
     scripts/build-crown-container.sh \
     scripts/verify-firmware-inputs.py \
     scripts/verify-proprietary-files.sh \
     scripts/

ARG CROWN_CACHE_ID=lineageos-crown
ARG CROWN_BUILD_MODE=full
RUN --mount=type=cache,id=${CROWN_CACHE_ID},target=/home/r0rt1z2,sharing=locked \
    --mount=type=bind,from=firmware,target=/firmware,readonly \
    /opt/lineageos-camera/scripts/build-crown-container.sh \
        /home/r0rt1z2/lineage-18.1 \
        /firmware \
        /artifacts \
        ${CROWN_BUILD_MODE}

FROM scratch AS crown-artifacts
COPY --from=crown-builder /artifacts/ /

FROM scratch AS artifacts
COPY --from=builder /artifacts/ /
