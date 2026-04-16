FROM nvidia/cuda:12.9.0-cudnn-devel-ubuntu24.04

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    git \ 
    libnvjpeg2k0-dev-cuda-12 \
    python3-numpy python3-skimage python3-scipy python3-pip

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src

COPY /src /workspace/src

RUN cmake -DCMAKE_BUILD_TYPE=Release -S /workspace/src -B /workspace/build; \
    cmake --build /workspace/build --config Release --parallel

COPY nvjpeg2k_benchmark.py /workspace/benchmark.py

WORKDIR /workspace
CMD ["/bin/bash"]
