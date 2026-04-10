FROM ubuntu:latest

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    wget \
    python3-numpy python3-skimage python3-scipy python3-pip

RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace/libjpeg
RUN wget -q -O libjpeg.tar.gz https://ijg.org/files/jpegsrc.v10.tar.gz
RUN tar -xf libjpeg.tar.gz --strip-components=1 -C /workspace/libjpeg && rm libjpeg.tar.gz
RUN ./configure && make -j$(nproc) && make install && ldconfig

COPY /src /workspace/src

RUN cmake -DCMAKE_BUILD_TYPE=Release -S /workspace/src -B /workspace/build; \
    cmake --build /workspace/build --config Release --parallel

COPY libjpeg_benchmark.py /workspace/benchmark.py

WORKDIR /workspace
CMD ["/bin/bash"]
