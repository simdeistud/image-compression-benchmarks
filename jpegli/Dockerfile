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

<<<<<<< HEAD
RUN mkdir -p /workspace/results /workspace/imageset /workspace/src
WORKDIR /workspace
RUN git clone --recursive --shallow-submodules https://github.com/google/jpegli.git
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Releases -DBUILD_TESTING=OFF -DJPEGXL_ENABLE_TOOLS=OFF -DJPEGXL_INSTALL_JPEGLI_LIBJPEG=ON -S jpegli -B jpegli/build; \
    cmake --build jpegli/build --config Release --parallel; \
    cmake --install jpegli/build
=======
RUN set -eux;                 git clone --recursive --depth 1 --branch main https://github.com/google/jpegli.git /tmp/codec-src;                 if [[ "${CHANNEL}" == "stable" ]]; then                   git -C /tmp/codec-src checkout main;                 else                   git -C /tmp/codec-src checkout main;                 fi;                 cmake -S /tmp/codec-src -B /tmp/codec-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTING=OFF;                 cmake --build /tmp/codec-build --config Release -j"$(nproc)";                 cmake --install /tmp/codec-build;                 ldconfig;                 rm -rf /tmp/codec-src /tmp/codec-build
>>>>>>> 2af8095ccb0edf29c2be9e173cf9f54fd4df980a

WORKDIR /workspace
CMD ["/bin/bash"]
