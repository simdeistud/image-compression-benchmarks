FROM ubuntu:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    libavif-dev

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
#WORKDIR /workspace
#RUN git clone https://github.com/AOMediaCodec/libavif.git
#RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release \
#    -DAVIF_CODEC_AOM=SYSTEM \
#    -S libavif -B libavif/build; \
#    cmake --build libavif/build --config Release --parallel; \
#    cmake --install libavif/build

WORKDIR /workspace
CMD ["/bin/bash"]
