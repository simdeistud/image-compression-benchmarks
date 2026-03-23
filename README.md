# image-compression-benchmarks

## C Programs Design

- If a library uses the CPU and supports multithreading, we will only use one thread
- In the benchmark loop, the input image is pre-allocated and remains the same. Until now, no cache optimizations have been found to skewer real world results
- Since not all libraries support pre-allocating the output buffer, we will let them manage the memory to ensure homogeneity in testing
- The benchmark loop is as bare as possible. If you wish to understand the program's choices look below at the single image implementation neatly divided into sections

## Python Script Design