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

<<<<<<< HEAD
WORKDIR /workspace
=======
RUN set -eux;                 git clone --depth 1 --branch master https://github.com/mozilla/mozjpeg.git /tmp/codec-src;                 if [[ "${CHANNEL}" == "stable" ]]; then                   git -C /tmp/codec-src fetch --depth 1 origin tag v4.1.5;                   git -C /tmp/codec-src checkout v4.1.5;                 else                   git -C /tmp/codec-src checkout master;                 fi;                 cmake -S /tmp/codec-src -B /tmp/codec-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_C_FLAGS="-fPIC";                 cmake --build /tmp/codec-build --config Release -j"$(nproc)";                 cmake --install /tmp/codec-build;                 ldconfig;                 rm -rf /tmp/codec-src /tmp/codec-build

WORKDIR /workspace/src
COPY ./src/mozjpeg_encode.cpp ./src/mozjpeg_decode.cpp ./src/CMakeLists.txt ./src/mozjpeg_benchmark.py /workspace/src/

RUN cmake -S /workspace/src -B /workspace/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /workspace/build --config Release -j"$(nproc)"

RUN mkdir -p /workspace/results /workspace/imageset

WORKDIR /workspace/build
>>>>>>> 2af8095ccb0edf29c2be9e173cf9f54fd4df980a
CMD ["/bin/bash"]
