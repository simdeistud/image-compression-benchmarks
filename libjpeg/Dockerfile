FROM ubuntu:latest

ARG CHANNEL=stable

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    wget \
    ca-certificates \
    python3 \
    python3-numpy \
 && rm -rf /var/lib/apt/lists/*

RUN case "${CHANNEL}" in \
      stable|dev) ;; \
      *) echo "CHANNEL must be stable or dev" >&2; exit 1 ;; \
    esac

# IJG does not expose a verifiable public source-control branch for current libjpeg releases.
# Therefore both channels resolve to the latest official stable source tarball.
RUN set -eux; \
    if [[ "${CHANNEL}" == "stable" ]]; then \
      LIBJPEG_URL="https://ijg.org/files/jpegsrc.v10.tar.gz"; \
    else \
      LIBJPEG_URL="https://ijg.org/files/jpegsrc.v10.tar.gz"; \
    fi; \
    mkdir -p /tmp/libjpeg-src; \
    wget -O /tmp/libjpeg.tar.gz "${LIBJPEG_URL}"; \
    tar -xzf /tmp/libjpeg.tar.gz -C /tmp/libjpeg-src --strip-components=1; \
    cd /tmp/libjpeg-src; \
    ./configure --prefix=/usr/local; \
    make -j"$(nproc)"; \
    make install; \
    ldconfig; \
    rm -rf /tmp/libjpeg-src /tmp/libjpeg.tar.gz

WORKDIR /workspace/src
COPY ./src/libjpeg_encode.cpp ./src/libjpeg_decode.cpp ./src/CMakeLists.txt ./src/libjpeg_benchmark.py /workspace/src/

RUN cmake -S /workspace/src -B /workspace/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /workspace/build --config Release -j"$(nproc)"

RUN mkdir -p /workspace/results /workspace/imageset

WORKDIR /workspace/build
CMD ["/bin/bash"]
