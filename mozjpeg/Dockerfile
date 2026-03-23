FROM ubuntu:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    nasm \
    cmake \
    pkg-config \
    ca-certificates \
    git

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace
RUN git clone https://github.com/mozilla/mozjpeg.git
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DPNG_SUPPORTED=OFF -DCMAKE_C_FLAGS="-fPIC" -S mozjpeg -B mozjpeg/build; \
    cmake --build mozjpeg/build --config Release --parallel; \
    cmake --install mozjpeg/build

WORKDIR /workspace
CMD ["/bin/bash"]
