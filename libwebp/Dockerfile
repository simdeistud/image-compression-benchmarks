FROM ubuntu:24.04

ARG CHANNEL=stable

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libtool \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --break-system-packages --no-cache-dir \
    numpy \
    pandas \
    scikit-image

WORKDIR /tmp

RUN set -eux; \
    git clone https://chromium.googlesource.com/webm/libwebp libwebp; \
    cd libwebp; \
    if [ "${CHANNEL}" = "stable" ]; then \
        stable_tag="$(git tag | grep -E '^(v)?[0-9]+(\.[0-9]+)*$' | sed 's/^v//' | sort -V | tail -n 1)"; \
        if [ -n "${stable_tag}" ]; then \
            git checkout "v${stable_tag}" 2>/dev/null || git checkout "${stable_tag}"; \
        else \
            git checkout main; \
        fi; \
    elif [ "${CHANNEL}" = "dev" ]; then \
        git checkout main; \
    else \
        echo "Unsupported CHANNEL='${CHANNEL}'. Use stable or dev." >&2; \
        exit 1; \
    fi; \
    ./autogen.sh; \
    CFLAGS='-O3 -DNDEBUG' \
    CXXFLAGS='-O3 -DNDEBUG' \
    ./configure \
        --prefix=/usr/local \
        --disable-gif \
        --disable-gl \
        --disable-jpeg \
        --disable-libwebpdemux \
        --disable-libwebpextras \
        --disable-libwebpmux \
        --disable-near-lossless \
        --disable-png \
        --disable-sdl \
        --disable-static \
        --disable-threading \
        --disable-tiff; \
    make -j"$(nproc)"; \
    make install; \
    ldconfig; \
    rm -rf /tmp/libwebp

RUN mkdir -p /workspace/src /workspace/build /workspace/results /workspace/imageset

COPY ./src/libwebp_encode.cpp /workspace/src/libwebp_encode.cpp
COPY ./src/libwebp_decode.cpp /workspace/src/libwebp_decode.cpp
COPY ./src/CMakeLists.txt /workspace/src/CMakeLists.txt
COPY ./src/libwebp_benchmark.py /workspace/src/libwebp_benchmark.py

WORKDIR /workspace/build

RUN cmake -S /workspace/src -B /workspace/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /workspace/build --config Release -j"$(nproc)" \
 && chmod +x /workspace/src/libwebp_benchmark.py

ENV PATH="/workspace/build:${PATH}"

CMD ["/bin/bash"]
