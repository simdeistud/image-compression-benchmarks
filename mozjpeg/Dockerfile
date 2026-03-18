FROM ubuntu:latest

ARG CHANNEL=stable

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    git \
    ca-certificates \
    nasm \
    libpng-dev \
    python3 \
    python3-numpy \
 && rm -rf /var/lib/apt/lists/*

RUN case "${CHANNEL}" in \
      stable|dev) ;; \
      *) echo "CHANNEL must be stable or dev" >&2; exit 1 ;; \
    esac

RUN set -eux;                 git clone --depth 1 --branch master https://github.com/mozilla/mozjpeg.git /tmp/codec-src;                 if [[ "${CHANNEL}" == "stable" ]]; then                   git -C /tmp/codec-src fetch --depth 1 origin tag v4.1.5;                   git -C /tmp/codec-src checkout v4.1.5;                 else                   git -C /tmp/codec-src checkout master;                 fi;                 cmake -S /tmp/codec-src -B /tmp/codec-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_C_FLAGS="-fPIC";                 cmake --build /tmp/codec-build --config Release -j"$(nproc)";                 cmake --install /tmp/codec-build;                 ldconfig;                 rm -rf /tmp/codec-src /tmp/codec-build

WORKDIR /workspace/src
COPY ./src/mozjpeg_encode.cpp ./src/mozjpeg_decode.cpp ./src/CMakeLists.txt ./src/mozjpeg_benchmark.py /workspace/src/

RUN cmake -S /workspace/src -B /workspace/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /workspace/build --config Release -j"$(nproc)"

RUN mkdir -p /workspace/results /workspace/imageset

WORKDIR /workspace/build
CMD ["/bin/bash"]
