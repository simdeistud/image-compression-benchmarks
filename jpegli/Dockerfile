FROM ubuntu:latest

ARG CHANNEL=stable

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    git \
    ca-certificates \
    python3 \
    python3-numpy \
 && rm -rf /var/lib/apt/lists/*

RUN case "${CHANNEL}" in \
      stable|dev) ;; \
      *) echo "CHANNEL must be stable or dev" >&2; exit 1 ;; \
    esac

RUN set -eux;                 git clone --recursive --depth 1 --branch main https://github.com/google/jpegli.git /tmp/codec-src;                 if [[ "${CHANNEL}" == "stable" ]]; then                   git -C /tmp/codec-src checkout main;                 else                   git -C /tmp/codec-src checkout main;                 fi;                 cmake -S /tmp/codec-src -B /tmp/codec-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTING=OFF;                 cmake --build /tmp/codec-build --config Release -j"$(nproc)";                 cmake --install /tmp/codec-build;                 ldconfig;                 rm -rf /tmp/codec-src /tmp/codec-build

WORKDIR /workspace/src
COPY ./src/jpegli_encode.cpp ./src/jpegli_decode.cpp ./src/CMakeLists.txt ./src/jpegli_benchmark.py /workspace/src/

RUN cmake -S /workspace/src -B /workspace/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /workspace/build --config Release -j"$(nproc)"

RUN mkdir -p /workspace/results /workspace/imageset

WORKDIR /workspace/build
CMD ["/bin/bash"]
