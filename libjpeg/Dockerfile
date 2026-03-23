FROM ubuntu:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    wget

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace/libjpeg
RUN wget -q -O libjpeg.tar.gz https://ijg.org/files/jpegsrc.v10.tar.gz
RUN tar -xf libjpeg.tar.gz --strip-components=1 -C /workspace/libjpeg && rm libjpeg.tar.gz
RUN ./configure && make -j$(nproc) && make install && ldconfig

WORKDIR /workspace
CMD ["/bin/bash"]
