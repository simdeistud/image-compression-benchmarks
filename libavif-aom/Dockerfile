FROM ubuntu:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    nasm \
    perl \
    cmake \
    pkg-config \
    ca-certificates \
    git

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace

RUN git clone https://github.com/AOMediaCodec/libavif.git
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release \
    -DAVIF_LIBYUV=LOCAL \
    -DAVIF_CODEC_AOM=LOCAL \
    -DAVIF_JPEG=OFF \
    -DAVIF_ZLIBPNG=OFF \
    -S libavif -B libavif/build; \
    cmake --build libavif/build --config Release --parallel; \
    cmake --install libavif/build

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/local.conf && ldconfig

WORKDIR /workspace
CMD ["/bin/bash"]
