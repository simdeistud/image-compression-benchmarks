FROM ubuntu:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    nasm \
    cmake \
    pkg-config \
    ca-certificates \
    git \
    python3-numpy python3-skimage python3-scipy python3-pip

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace

RUN git clone https://github.com/libjpeg-turbo/libjpeg-turbo.git
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -S libjpeg-turbo -B libjpeg-turbo/build; \
    cmake --build libjpeg-turbo/build --config Release --parallel; \
    cmake --install libjpeg-turbo/build

COPY /src /workspace/src

RUN cmake -DCMAKE_BUILD_TYPE=Release -S /workspace/src -B /workspace/build; \
    cmake --build /workspace/build --config Release --parallel

COPY libjpeg-turbo_benchmark.py /workspace/benchmark.py

WORKDIR /workspace
CMD ["/bin/bash"]
