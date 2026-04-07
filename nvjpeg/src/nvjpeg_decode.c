#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#define IMG_IO_IMPLEMENTATION
#include "img_io.h"
#include <nvjpeg.h>
#include <cuda_runtime_api.h>

#define CHECK_CUDA(cmd)                                                         \
do {                                                                            \
    cudaError_t e = (cmd);                                                      \
    if (e != cudaSuccess) {                                                     \
        fprintf(stderr, "CUDA error: %s at %s:%d\n", cudaGetErrorString(e),     \
                __FILE__, __LINE__);                                            \
        return 1;                                                               \
    }                                                                           \
} while (0)

#define CHECK_NVJPEG(cmd)                                                       \
do {                                                                            \
    nvjpegStatus_t s = (cmd);                                                   \
    if (s != NVJPEG_STATUS_SUCCESS) {                                           \
        fprintf(stderr, "nvJPEG error: %d at %s:%d\n", (int)s,                  \
                __FILE__, __LINE__);                                            \
        return 1;                                                               \
    }                                                                           \
} while (0)

// ---------- allocators required by nvjpegCreateEx ----------
static int dev_malloc(void **p, size_t s)
{
    return (int)cudaMalloc(p, s);
}
static int dev_free(void *p)
{
    return (int)cudaFree(p);
}
// Use simple malloc/free for nvJPEG internal pinned allocator here;
// we will explicitly allocate our I/O buffers as pinned via cudaHostAlloc.
static int host_malloc(void **p, size_t s, unsigned int flags)
{
    (void)flags;
    *p = malloc(s);
    return (*p) ? 0 : 1;
}
static int host_free(void *p)
{
    free(p);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Basic options:\n"
            "  --iterations <int>         Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                Benchmark mode (flag)\n"
            "  --input <path>|-           Selected jpeg [PATH|stdin]\n"
            "  --output <path>|-          Selected RGBI24 [PATH|stdout]\n"
            "  --help                     Show this help and exit\n",
            prog);
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX)
    {
        return -1;
    }
    *out = (int)v;
    return 0;
}

int main(int argc, char *argv[])
{
    int iterations = 0;
    int benchmark = 0;
    char *jpeg_input_path = NULL;
    char *rgbi24_output_path = NULL;

    /* === ARGUMENT PARSING === */

    /* Long-only; provide no short options (optstring = "") */
    static const struct option long_opts[] = {
        {"iterations", required_argument, NULL, 1},
        {"benchmark", no_argument, NULL, 2},
        {"input", required_argument, NULL, 3},
        {"output", required_argument, NULL, 4},
        {"help", no_argument, NULL, 5},
        {0, 0, 0, 0}};

    int opt, longidx;
    /* Reset getopt state if needed (when embedding): optind = 1; */
    while ((opt = getopt_long(argc, argv, "", long_opts, &longidx)) != -1)
    {
        switch (opt)
        {
        case 1:
        { /* --iterations */
            if (parse_int(optarg, &iterations) != 0)
            {
                fprintf(stderr, "Invalid --iterations value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 2: /* --benchmark */
            benchmark = 1;
            break;
        case 3: /* --input */
            jpeg_input_path = optarg;
            break;
        case 4: /* --output */
            rgbi24_output_path = optarg;
            break;
        case 5: /* --help */
            usage(argv[0]);
            return EXIT_SUCCESS;
        case '?': /* getopt_long already printed an error */
        default:
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* Reject unexpected positional args */
    if (optind < argc)
    {
        fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (benchmark)
    {
        /* === DECODER BENCHMARK === */
        cudaStream_t stream = 0;
        nvjpegHandle_t handle = NULL;
        nvjpegJpegState_t jpeg_state = NULL;
        nvjpegDevAllocator_t dev_alloc = { dev_malloc, dev_free };
        nvjpegPinnedAllocator_t pinned_alloc = { host_malloc, host_free };

        unsigned char *jpeg_input = NULL;
        size_t jpeg_input_size = 0;
        if (!strcmp(jpeg_input_path, "-"))
        {
            int err = load_img_from_stdin(&jpeg_input, &jpeg_input_size);
            if (err)
            {
                return err;
            }
        }
        else
        {
            int err = load_img_from_path(jpeg_input_path, &jpeg_input, &jpeg_input_size);
            if (err)
            {
                return err;
            }
        }

        CHECK_NVJPEG(nvjpegCreateEx(
            NVJPEG_BACKEND_GPU_HYBRID_DEVICE,   // or NVJPEG_BACKEND_HARDWARE if supported
            &dev_alloc,
            &pinned_alloc,
            NVJPEG_FLAGS_DEFAULT,
            &handle
        ));
        CHECK_NVJPEG(nvjpegJpegStateCreate(handle, &jpeg_state));
        

        int widths[NVJPEG_MAX_COMPONENT] = {0};
        int heights[NVJPEG_MAX_COMPONENT] = {0};
        int components = 0;
        nvjpegChromaSubsampling_t subsampling;
        nvjpegOutputFormat_t output_format = NVJPEG_OUTPUT_RGBI;
        CHECK_NVJPEG(nvjpegGetImageInfo(handle, jpeg_input, jpeg_input_size, &components, &subsampling, widths, heights));

        
        
        unsigned char* rgbi24_output_device = NULL;
        size_t rgbi24_output_size = widths[0] * heights[0] * 3;
        CHECK_CUDA(cudaMalloc((void**)&rgbi24_output_device, rgbi24_output_size));
        unsigned char* rgbi24_output = malloc(rgbi24_output_size);
        

        nvjpegImage_t outimg;
        outimg.channel[0] = rgbi24_output_device;
        outimg.pitch[0]   = widths[0] * 3;
        clock_t total_processing_time = 0;
        int width = widths[0], height = heights[0];
        for(int i = 0; i < iterations; ++i)
        {
            clock_t t0 = clock();
            nvjpegDecode(handle, jpeg_state, jpeg_input, jpeg_input_size, output_format, &outimg, stream);
            cudaMemcpy(rgbi24_output, outimg.channel[0], rgbi24_output_size, cudaMemcpyDeviceToHost);
            clock_t t1 = clock();
            total_processing_time += t1 - t0;
        }
        nvjpegJpegStateDestroy(jpeg_state);
        CHECK_CUDA(cudaFree(rgbi24_output_device));
        printf("Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        printf("Average processing time per iteration (milliseconds):%f\n", ((double)total_processing_time) / iterations / CLOCKS_PER_SEC * 1000);
        printf("Average frames per second:%f\n", iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        printf("Average megapixels per second:%f\n", (width * height) / (double)1000000 * iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        img_destroy(jpeg_input);
    }

    /* === DECODER SETUP === */
    cudaStream_t stream = 0;
    nvjpegHandle_t handle = NULL;
    nvjpegJpegState_t jpeg_state = NULL;
    nvjpegDevAllocator_t dev_alloc = { dev_malloc, dev_free };
    nvjpegPinnedAllocator_t pinned_alloc = { host_malloc, host_free };

    unsigned char *jpeg_input = NULL;
    size_t jpeg_input_size = 0;
    if (!strcmp(jpeg_input_path, "-"))
    {
        int err = load_img_from_stdin(&jpeg_input, &jpeg_input_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = load_img_from_path(jpeg_input_path, &jpeg_input, &jpeg_input_size);
        if (err)
        {
            return err;
        }
    }

    CHECK_NVJPEG(nvjpegCreateEx(
        NVJPEG_BACKEND_GPU_HYBRID_DEVICE,   // or NVJPEG_BACKEND_HARDWARE if supported
        &dev_alloc,
        &pinned_alloc,
        NVJPEG_FLAGS_DEFAULT,
        &handle
    ));
    CHECK_NVJPEG(nvjpegJpegStateCreate(handle, &jpeg_state));
    

    int widths[NVJPEG_MAX_COMPONENT] = {0};
    int heights[NVJPEG_MAX_COMPONENT] = {0};
    int components = 0;
    nvjpegChromaSubsampling_t subsampling;
    nvjpegOutputFormat_t output_format = NVJPEG_OUTPUT_RGBI;
    CHECK_NVJPEG(nvjpegGetImageInfo(handle, jpeg_input, jpeg_input_size, &components, &subsampling, widths, heights));

    
    
    unsigned char* rgbi24_output_device = NULL;
    size_t rgbi24_output_size = widths[0] * heights[0] * 3;
    CHECK_CUDA(cudaMalloc((void**)&rgbi24_output_device, rgbi24_output_size));
    unsigned char* rgbi24_output = malloc(rgbi24_output_size);
    

    nvjpegImage_t outimg;
    outimg.channel[0] = rgbi24_output_device;
    outimg.pitch[0]   = widths[0] * 3;
    

    /* === DECODER TEST === */
    CHECK_NVJPEG(nvjpegDecode(handle, jpeg_state, jpeg_input, jpeg_input_size, output_format, &outimg, stream));
    CHECK_CUDA(cudaMemcpy(rgbi24_output, outimg.channel[0], rgbi24_output_size, cudaMemcpyDeviceToHost));

    /* === DECODER CLEANUP === */
    nvjpegJpegStateDestroy(jpeg_state);
    img_destroy(jpeg_input);
    CHECK_CUDA(cudaFree(rgbi24_output_device));

    /* === DECODED IMAGE OUTPUT === */
    if (!strcmp(rgbi24_output_path, "-"))
    {
        int err = write_img_to_stdout(rgbi24_output, rgbi24_output_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = write_img_to_path(rgbi24_output_path, rgbi24_output, rgbi24_output_size);
        if (err)
        {
            return err;
        }
    }
    free(rgbi24_output);

    return EXIT_SUCCESS;
}