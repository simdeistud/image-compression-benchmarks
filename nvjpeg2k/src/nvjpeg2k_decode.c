#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#define IMG_IO_IMPLEMENTATION
#include "img_io.h"
#include <nvjpeg2k.h>
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

#define CHECK_NVJPEG2K(cmd)                                                       \
do {                                                                            \
    nvjpeg2kStatus_t s = (cmd);                                                   \
    if (s != NVJPEG2K_STATUS_SUCCESS) {                                           \
        fprintf(stderr, "nvjpeg2K error: %d at %s:%d\n", (int)s,                  \
                __FILE__, __LINE__);                                            \
        return 1;                                                               \
    }                                                                           \
} while (0)

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Basic options:\n"
            "  --iterations <int>         Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                Benchmark mode (flag)\n"
            "  --input <path>|-           Selected jpeg2k [PATH|stdin]\n"
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
    char *jpeg2k_input_path = NULL;
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
            jpeg2k_input_path = optarg;
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
       
        clock_t total_processing_time = 0;
        int width = 0, height = 0;
        for(int i = 0; i < iterations; ++i)
        {
            clock_t t0 = clock();
            
            clock_t t1 = clock();
            total_processing_time += t1 - t0;
        }
        
        printf("Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        printf("Average processing time per iteration (milliseconds):%f\n", ((double)total_processing_time) / iterations / CLOCKS_PER_SEC * 1000);
        printf("Average frames per second:%f\n", iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        printf("Average megapixels per second:%f\n", (width * height) / (double)1000000 * iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        //img_destroy(jpeg2k_input);
    }

    /* === DECODER SETUP === */
    cudaStream_t stream = 0;
    nvjpeg2kHandle_t handle = NULL;
    nvjpeg2kDecodeState_t decode_state = NULL;
    nvjpeg2kStream_t jpeg2k_stream = NULL;
    nvjpeg2kDecodeParams_t decode_params = NULL;

    CHECK_NVJPEG2K(nvjpeg2kCreateSimple(&handle));
    CHECK_NVJPEG2K(nvjpeg2kDecodeStateCreate(handle, &decode_state));
    CHECK_NVJPEG2K(nvjpeg2kStreamCreate(&jpeg2k_stream));
    CHECK_NVJPEG2K(nvjpeg2kDecodeParamsCreate(&decode_params));

    unsigned char *jpeg2k_input = NULL;
    size_t jpeg2k_input_size = 0;
    if (!strcmp(jpeg2k_input_path, "-"))
    {
        int err = load_img_from_stdin(&jpeg2k_input, &jpeg2k_input_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = load_img_from_path(jpeg2k_input_path, &jpeg2k_input, &jpeg2k_input_size);
        if (err)
        {
            return err;
        }
    }

    CHECK_NVJPEG2K(nvjpeg2kStreamParse(
        handle, jpeg2k_input, jpeg2k_input_size, 0, 0, jpeg2k_stream));

    nvjpeg2kImageInfo_t image_info;
    CHECK_NVJPEG2K(nvjpeg2kStreamGetImageInfo(jpeg2k_stream, &image_info));

    CHECK_NVJPEG2K(nvjpeg2kDecodeParamsSetRGBOutput(decode_params, 1));
    CHECK_NVJPEG2K(nvjpeg2kDecodeParamsSetOutputFormat(
        decode_params, NVJPEG2K_FORMAT_INTERLEAVED));    
    
    unsigned char* rgbi24_output_device = NULL;
    size_t rgbi24_output_size = image_info.image_width * image_info.image_height * 3;
    CHECK_CUDA(cudaMalloc((void**)&rgbi24_output_device, rgbi24_output_size));
    unsigned char* rgbi24_output = malloc(rgbi24_output_size);
    

    nvjpeg2kImage_t outimg;
    outimg.pixel_data[0] = rgbi24_output_device;
    outimg.pitch_in_bytes[0] = image_info.image_width * 3;
    outimg.pixel_type = NVJPEG2K_UINT8;
    outimg.num_components = 3;
    
    /* === DECODER TEST === */
    CHECK_NVJPEG2K(nvjpeg2kDecodeImage(handle, decode_state, jpeg2k_stream, decode_params, &outimg, stream));
    CHECK_CUDA(cudaMemcpy(rgbi24_output, rgbi24_output_device, rgbi24_output_size, cudaMemcpyDeviceToHost));

    /* === DECODER CLEANUP === */
    CHECK_CUDA(cudaFree(rgbi24_output_device));
    CHECK_NVJPEG2K(nvjpeg2kDecodeParamsDestroy(decode_params));
    CHECK_NVJPEG2K(nvjpeg2kStreamDestroy(jpeg2k_stream));
    CHECK_NVJPEG2K(nvjpeg2kDecodeStateDestroy(decode_state));
    CHECK_NVJPEG2K(nvjpeg2kDestroy(handle));

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