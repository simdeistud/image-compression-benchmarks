# Ambiguities and design notes

1. **Stable vs. dev source selection**: the official `libjxl` repository exposes a public `main` branch. The Dockerfile uses the newest official release tag for `CHANNEL=stable` and the official public `main` branch for `CHANNEL=dev`.
2. **Quality mapping**: libjxl's native user-facing quality concept is primarily `distance`, not a strict JPEG-style quality value. The encoder program exposes `--quality` and maps it to libjxl distance using the same public approximation used by FFmpeg's libjxl integration.
3. **Threading**: the request explicitly requires single-thread CPU execution, so the benchmark programs deliberately do not install a libjxl parallel runner.
4. **Fastest options beyond documented primary knobs**: the programs force the main documented speed knobs (`effort=1`, `decoding_speed=4`) and otherwise keep the remaining coding tools at library defaults, because libjxl does not document a universal globally-optimal fastest setting for every individual coding tool across all image classes.
5. **Lossless quality=100 behavior**: the benchmark sweep only uses qualities 10..90 as requested. The encoder accepts 100, but the report notes that the quality mapping is distance-based rather than promising bit-exact lossless JPEG-XL behavior for all inputs.
