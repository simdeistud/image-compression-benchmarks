ARG CHANNEL=stable
FROM ubuntu:25.10
ARG CHANNEL

ENV DEBIAN_FRONTEND=noninteractive
ENV CC=clang
ENV CXX=clang++

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        cmake \
        git \
        libbrotli-dev \
        ninja-build \
        pkg-config \
        python3 \
        python3-numpy \
        python3-skimage \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --recursive --shallow-submodules https://github.com/libjxl/libjxl.git /tmp/libjxl \
    && cd /tmp/libjxl \
    && git fetch --tags --force \
    && if [ "${CHANNEL}" = "stable" ]; then \
         latest_tag="$(git tag --list 'v[0-9]*.[0-9]*.[0-9]*' | sort -V | tail -n 1)"; \
         if [ -z "${latest_tag}" ]; then echo 'Unable to determine a stable libjxl tag'; exit 1; fi; \
         git checkout --detach "${latest_tag}"; \
         git submodule update --init --recursive --depth 1 --recommend-shallow; \
       elif [ "${CHANNEL}" = "dev" ]; then \
         if git ls-remote --exit-code --heads origin main >/dev/null 2>&1; then \
           git checkout main; \
           git submodule update --init --recursive --depth 1 --recommend-shallow; \
         else \
           echo 'Unable to determine a public development branch'; exit 1; \
         fi; \
       else \
         echo 'CHANNEL must be either stable or dev'; exit 1; \
       fi \
    && cmake -S /tmp/libjxl -B /tmp/libjxl-build -G Ninja \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=/usr/local \
         -DBUILD_SHARED_LIBS=ON \
         -DBUILD_TESTING=OFF \
         -DJPEGXL_ENABLE_TOOLS=OFF \
         -DJPEGXL_ENABLE_DEVTOOLS=OFF \
         -DJPEGXL_ENABLE_TRANSCODE_JPEG=OFF \
         -DJPEGXL_ENABLE_BOXES=OFF \
         -DJPEGXL_ENABLE_SKCMS=OFF \
         -DJPEGXL_ENABLE_SJPEG=OFF \
    && cmake --build /tmp/libjxl-build --parallel \
    && cmake --install /tmp/libjxl-build \
    && ldconfig \
    && rm -rf /tmp/libjxl /tmp/libjxl-build

RUN mkdir -p /workspace/src /workspace/build /workspace/results /workspace/imageset

COPY ./src/libjxl_encode.cpp /workspace/src/libjxl_encode.cpp
COPY ./src/libjxl_decode.cpp /workspace/src/libjxl_decode.cpp
COPY ./src/CMakeLists.txt /workspace/src/CMakeLists.txt
COPY ./src/libjxl_benchmark.py /workspace/src/libjxl_benchmark.py

RUN cmake -S /workspace/src -B /workspace/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /workspace/build --parallel

WORKDIR /workspace
CMD ["/bin/bash"]
