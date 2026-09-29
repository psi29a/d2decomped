FROM ubuntu:26.04

# Deps for the CI Linux job (build, tidy, ctest). Baked once so
# validate-linux.sh doesn't pay apt-get on every run. Rebuild by touching
# this file, then rerunning validate-linux.sh (it checks the digest).
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        ca-certificates cmake ninja-build build-essential pkg-config \
        git python3 findutils \
        libsdl3-dev libopenal-dev zlib1g-dev libbz2-dev libstorm-dev \
        qt6-base-dev libgl1-mesa-dev \
        libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
        clang-tidy && \
    rm -rf /var/lib/apt/lists/*
