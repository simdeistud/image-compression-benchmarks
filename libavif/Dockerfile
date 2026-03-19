# syntax=docker/dockerfile:1
FROM ubuntu:latest

ARG CHANNEL=stable
ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    ca-certificates \
    cargo \
    clang \
    cmake \
    curl \
    git \
    meson \
    nasm \
    ninja-build \
    pkg-config \
    python3 \
    python3-dev \
    python3-pip \
    python3-venv \
    python3-numpy \
    python3-pandas \
    python3-skimage \
    rustc \
    yasm \
 && rm -rf /var/lib/apt/lists/*

RUN cat >/usr/local/bin/clone_channel.sh <<'EOF' && chmod +x /usr/local/bin/clone_channel.sh
#!/usr/bin/env bash
set -euo pipefail
repo_url="$1"
dst_dir="$2"
channel="${3:-stable}"

rm -rf "$dst_dir"
git clone --filter=blob:none "$repo_url" "$dst_dir"
cd "$dst_dir"

if [[ "$channel" == "stable" ]]; then
  latest_tag="$(git ls-remote --tags --refs origin \
    | awk '{print $2}' \
    | sed 's#refs/tags/##' \
    | grep -Ev '(-rc|-alpha|-beta|\\^\\{\\})' \
    | sort -V \
    | tail -n1 || true)"
  if [[ -n "$latest_tag" ]]; then
    git checkout --detach "$latest_tag"
  else
    default_branch="$(git symbolic-ref --short refs/remotes/origin/HEAD | sed 's#^origin/##')"
    git checkout "$default_branch"
  fi
elif [[ "$channel" == "dev" ]]; then
  default_branch="$(git symbolic-ref --short refs/remotes/origin/HEAD | sed 's#^origin/##')"
  git checkout "$default_branch"
else
  echo "Unsupported CHANNEL: $channel (expected stable or dev)" >&2
  exit 2
fi
EOF

RUN python3 -m pip install --no-cache-dir --upgrade pip setuptools wheel cargo-c

ENV CFLAGS="-O3 -DNDEBUG -march=native -mtune=native -fPIC" \
    CXXFLAGS="-O3 -DNDEBUG -march=native -mtune=native -fPIC" \
    RUSTFLAGS="-C target-cpu=native"

WORKDIR /tmp/builddeps

RUN /usr/local/bin/clone_channel.sh https://aomedia.googlesource.com/aom /tmp/builddeps/aom ${CHANNEL} \
 && cmake -S /tmp/builddeps/aom -B /tmp/builddeps/aom/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      -DBUILD_SHARED_LIBS=ON \
      -DENABLE_DOCS=OFF \
      -DENABLE_EXAMPLES=OFF \
      -DENABLE_TESTDATA=OFF \
      -DENABLE_TESTS=OFF \
      -DENABLE_TOOLS=OFF \
      -DENABLE_NASM=ON \
 && cmake --build /tmp/builddeps/aom/build --parallel \
 && cmake --install /tmp/builddeps/aom/build

RUN /usr/local/bin/clone_channel.sh https://code.videolan.org/videolan/dav1d.git /tmp/builddeps/dav1d ${CHANNEL} \
 && meson setup /tmp/builddeps/dav1d/build /tmp/builddeps/dav1d \
      --prefix=/usr/local \
      --buildtype=release \
      -Ddefault_library=shared \
      -Denable_tools=false \
      -Denable_tests=false \
      -Denable_examples=false \
 && ninja -C /tmp/builddeps/dav1d/build \
 && ninja -C /tmp/builddeps/dav1d/build install

RUN /usr/local/bin/clone_channel.sh https://chromium.googlesource.com/codecs/libgav1 /tmp/builddeps/libgav1 ${CHANNEL} \
 && cmake -S /tmp/builddeps/libgav1 -B /tmp/builddeps/libgav1/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      -DBUILD_SHARED_LIBS=ON \
      -DLIBGAV1_ENABLE_EXAMPLES=OFF \
      -DLIBGAV1_ENABLE_TESTS=OFF \
 && cmake --build /tmp/builddeps/libgav1/build --parallel \
 && cmake --install /tmp/builddeps/libgav1/build

RUN /usr/local/bin/clone_channel.sh https://github.com/xiph/rav1e.git /tmp/builddeps/rav1e ${CHANNEL} \
 && cargo cinstall --manifest-path /tmp/builddeps/rav1e/Cargo.toml \
      --prefix /usr/local \
      --release \
      --library-type cdylib

RUN /usr/local/bin/clone_channel.sh https://gitlab.com/AOMediaCodec/SVT-AV1.git /tmp/builddeps/SVT-AV1 ${CHANNEL} \
 && cmake -S /tmp/builddeps/SVT-AV1 -B /tmp/builddeps/SVT-AV1/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      -DBUILD_SHARED_LIBS=ON \
      -DBUILD_APPS=OFF \
      -DBUILD_TESTING=OFF \
 && cmake --build /tmp/builddeps/SVT-AV1/build --parallel \
 && cmake --install /tmp/builddeps/SVT-AV1/build

RUN /usr/local/bin/clone_channel.sh https://github.com/AOMediaCodec/libavif.git /tmp/builddeps/libavif ${CHANNEL} \
 && cmake -S /tmp/builddeps/libavif -B /tmp/builddeps/libavif/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      -DBUILD_SHARED_LIBS=ON \
      -DAVIF_CODEC_AOM=SYSTEM \
      -DAVIF_CODEC_DAV1D=SYSTEM \
      -DAVIF_CODEC_LIBGAV1=SYSTEM \
      -DAVIF_CODEC_RAV1E=SYSTEM \
      -DAVIF_CODEC_SVT=SYSTEM \
      -DAVIF_CODEC_AVM=OFF \
      -DAVIF_BUILD_APPS=OFF \
      -DAVIF_BUILD_EXAMPLES=OFF \
      -DAVIF_BUILD_TESTS=OFF \
      -DAVIF_ENABLE_GTEST=OFF \
      -DAVIF_LIBYUV=OFF \
      -DAVIF_JPEG=OFF \
      -DAVIF_ZLIBPNG=OFF \
 && cmake --build /tmp/builddeps/libavif/build --parallel \
 && cmake --install /tmp/builddeps/libavif/build \
 && ldconfig

WORKDIR /workspace
RUN mkdir -p /workspace/src /workspace/build /workspace/results /workspace/imageset
COPY ./src/libavif_encode.cpp /workspace/src/libavif_encode.cpp
COPY ./src/libavif_decode.cpp /workspace/src/libavif_decode.cpp
COPY ./src/CMakeLists.txt /workspace/src/CMakeLists.txt
COPY ./src/libavif_benchmark.py /workspace/src/libavif_benchmark.py

RUN cat >/usr/local/bin/build_project.sh <<'EOF' && chmod +x /usr/local/bin/build_project.sh
#!/usr/bin/env bash
set -euo pipefail
cmake -S /workspace/src -B /workspace/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /workspace/build --parallel
cp -f /workspace/build/libavif_encode /workspace/results/
cp -f /workspace/build/libavif_decode /workspace/results/
cp -f /workspace/src/libavif_benchmark.py /workspace/results/
echo "Artifacts copied to /workspace/results"
EOF

VOLUME ["/workspace/results", "/workspace/imageset"]
CMD ["/usr/local/bin/build_project.sh"]
