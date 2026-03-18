# Ambiguity Report

1. **Stable vs dev libjpeg source**
   - The official IJG site clearly exposes the current stable source tarball (`jpegsrc.v10.tar.gz`).
   - A verifiable public source-control repository or branch for current IJG libjpeg development is not clearly exposed.
   - To satisfy the required `CHANNEL=stable|dev` contract, both channels currently resolve to the same latest official stable IJG source in the Dockerfile.

2. **Benchmark stdout vs encoded/decoded payload**
   - The requirements state that benchmark mode must print timing metrics to stdout.
   - The benchmarking script requirements also say the encoded and decoded image should be saved from stdout.
   - These two constraints conflict. The provided Python script resolves this by running each executable twice per configuration:
     - once in benchmark mode to capture metrics,
     - once in normal mode with `-o -` to capture the binary payload.

3. **FPS / megapixels per second definition**
   - The requirements ask for FPS and MPix/s, but they do not define whether those should be based on pure encode/decode time only or on full per-image iteration time.
   - The script computes throughput using the full average measured iteration time: `SETUP_TIME + PROCESS_TIME + RESET_TIME`.

4. **Restart interval semantic**
   - The requirement names `-r` as a restart interval integer but does not specify units.
   - The implementation maps `-r` directly to libjpeg's `restart_interval` field.
