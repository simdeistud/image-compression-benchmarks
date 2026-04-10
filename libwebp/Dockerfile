FROM ubuntu:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    git \
    python3-numpy python3-skimage python3-scipy python3-pip

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace
RUN git clone https://github.com/webmproject/libwebp.git
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release \
    -DWEBP_BUILD_CWEBP=OFF \
    -DWEBP_BUILD_DWEBP=OFF \
    -DWEBP_BUILD_GIF2WEBP=OFF \
    -DWEBP_BUILD_IMG2WEBP=OFF \
    -DWEBP_BUILD_VWEBP=OFF \
    -DWEBP_BUILD_WEBPINFO=OFF \
    -DWEBP_BUILD_LIBWEBPMUX=OFF \
    -DWEBP_BUILD_WEBPMUX=OFF \
    -DWEBP_BUILD_EXTRAS=OFF \
    -DWEBP_BUILD_FUZZTEST=OFF \
    -DWEBP_USE_THREAD=OFF \
    -DWEBP_NEAR_LOSSLESS=OFF \
    -S libwebp -B libwebp/build; \
    cmake --build libwebp/build --config Release --parallel; \
    cmake --install libwebp/build

COPY /src /workspace/src

RUN cmake -DCMAKE_BUILD_TYPE=Release -S /workspace/src -B /workspace/build; \
    cmake --build /workspace/build --config Release --parallel

COPY libwebp_benchmark.py /workspace/benchmark.py

WORKDIR /workspace
CMD ["/bin/bash"]
