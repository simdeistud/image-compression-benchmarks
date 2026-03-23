# image-compression-benchmarks

This is a suite of benchmark for assessing the latency of single image encoding using a variety of codecs and their implementations. This is not a generalistic benchmarking suite: the timings refer to RAM-to-RAM processing (even with GPUs), we limit multithreading to 1, we use by default the fastest options available exposed by the libraries, and we only expose parameters that are reasonable a user in a low-latency scenario would want to touch.

## C Programs Design

- In the benchmark loop, the input image is pre-allocated and remains the same. Until now, no cache optimizations have been found to skewer real world results
- Since not all libraries support pre-allocating the output buffer, we will let them manage the memory to ensure homogeneity in testing
- The benchmark loop is as bare as possible. If you wish to understand the program's choices look below at the single image implementation neatly divided into sections

## Python Script Design