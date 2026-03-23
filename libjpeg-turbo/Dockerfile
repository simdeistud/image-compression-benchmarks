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
RUN git clone https://github.com/libjpeg-turbo/libjpeg-turbo.git
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -S libjpeg-turbo -B libjpeg-turbo/build; \
    cmake --build libjpeg-turbo/build --config Release --parallel; \
    cmake --install libjpeg-turbo/build

WORKDIR /workspace
CMD ["/bin/bash"]
