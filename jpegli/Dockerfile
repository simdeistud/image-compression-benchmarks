FROM ubuntu:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    cmake \
    pkg-config \
    ca-certificates \
    git

ENV CC=clang CXX=clang++

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace
RUN git clone --recursive --shallow-submodules https://github.com/google/jpegli.git
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Releases -DBUILD_TESTING=OFF -DJPEGXL_ENABLE_TOOLS=OFF -DJPEGXL_INSTALL_JPEGLI_LIBJPEG=ON -S jpegli -B jpegli/build; \
    cmake --build jpegli/build --config Release --parallel; \
    cmake --install jpegli/build

WORKDIR /workspace
CMD ["/bin/bash"]
