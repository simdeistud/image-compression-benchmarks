FROM nvidia/cuda:13.2.0-cudnn-devel-ubuntu24.04

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    git \ 
    libnvjpeg2k0-dev-cuda-13

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace
CMD ["/bin/bash"]
