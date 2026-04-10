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

static void usage(const char* prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --width <int>                Input image width (px)\n"
        "  --height <int>               Input image height (px)\n"
        "  --subsampling <int>          Selected subsampling [444|422|420]\n"
        "  --quality <int>              Selected quality [0...100]\n"
        "  --iterations <int>           Selected iterations [>0 if --benchmark]\n"
        "  --benchmark                  Benchmark mode (flag)\n"
        "  --input <path>|-             Selected jpeg input [PATH|stdin]\n"
        "  --output <path>|-            Selected RGBI24 output [PATH|stdout]\n"
        "  --help                       Show this help and exit\n",
        prog);
}

static int parse_int(const char* s, int* out)
{
    char* end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX)
    {
        return -1;
    }
    *out = (int)v;
    return 0;
}

nvjpegChromaSubsampling_t subsampling_to_nvjpegsamp(const int subsampling)
{
    switch (subsampling)
    {
    case 444: return NVJPEG_CSS_444;
    case 422: return NVJPEG_CSS_422;
    case 420: return NVJPEG_CSS_420;
    default: return NVJPEG_CSS_UNKNOWN;
    }
}

int main(int argc, char* argv[])
{
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int quality = 0;

    int iterations = 0;
    int benchmark = 0;
    char* rgbi24_input_path = NULL;
    char* jpeg_output_path = NULL;

    /* === ARGUMENT PARSING === */

    /* Long-only; provide no short options (optstring = "") */
    static const struct option long_opts[] = {
        {"width", required_argument, NULL, 1},
        {"height", required_argument, NULL, 2},
        {"subsampling", required_argument, NULL, 3},
        {"quality", required_argument, NULL, 4},
        {"iterations", required_argument, NULL, 5},
        {"benchmark", no_argument, NULL, 6},
        {"input", required_argument, NULL, 7},
        {"output", required_argument, NULL, 8},
        {"help", no_argument, NULL, 9},
        {0, 0, 0, 0} };

    int opt, longidx;
    /* Reset getopt state if needed (when embedding): optind = 1; */
    while ((opt = getopt_long(argc, argv, "", long_opts, &longidx)) != -1)
    {
        switch (opt)
        {
        case 1:
        { /* --width */
            if (parse_int(optarg, &width) != 0)
            {
                fprintf(stderr, "Invalid --width value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 2:
        { /* --height */
            if (parse_int(optarg, &height) != 0)
            {
                fprintf(stderr, "Invalid --height value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 3:
        { /* --subsampling */
            if (parse_int(optarg, &subsampling) != 0)
            {
                fprintf(stderr, "Invalid --subsampling value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 4:
        { /* --quality */
            if (parse_int(optarg, &quality) != 0)
            {
                fprintf(stderr, "Invalid --quality value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 5:
        { /* --iterations */
            if (parse_int(optarg, &iterations) != 0)
            {
                fprintf(stderr, "Invalid --iterations value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 6: /* --benchmark */
            benchmark = 1;
            break;
        case 7: /* --input */
            rgbi24_input_path = optarg;
            break;
        case 8: /* --output */
            jpeg_output_path = optarg;
            break;
        case 9: /* --help */
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

    unsigned char* inbuf = NULL;
    size_t inbuf_size = 0;
    if (!strcmp(rgbi24_input_path, "-"))
    {
        int err = load_img_from_stdin(&inbuf, &inbuf_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = load_img_from_path(rgbi24_input_path, &inbuf, &inbuf_size);
        if (err)
        {
            return err;
        }
    }

    if (benchmark) {
        /* === ENCODER BENCHMARK === */
        cudaStream_t stream = 0;
        nvjpegHandle_t handle = NULL;
        nvjpegEncoderState_t encoder_state = NULL;
        nvjpegEncoderParams_t encoder_params = NULL;

        CHECK_NVJPEG(nvjpegCreateSimple(&handle));
        CHECK_NVJPEG(nvjpegEncoderStateCreateWithBackend(handle, &encoder_state, NVJPEG_ENC_BACKEND_DEFAULT, stream));
        CHECK_NVJPEG(nvjpegEncoderParamsCreate(handle, &encoder_params, stream));
        CHECK_NVJPEG(nvjpegEncoderParamsSetEncoding(encoder_params, NVJPEG_ENCODING_BASELINE_DCT, stream));
        CHECK_NVJPEG(nvjpegEncoderParamsSetQuality(encoder_params, quality, stream));
        CHECK_NVJPEG(nvjpegEncoderParamsSetOptimizedHuffman(encoder_params, 0, stream));
        CHECK_NVJPEG(nvjpegEncoderParamsSetSamplingFactors(encoder_params, subsampling_to_nvjpegsamp(subsampling), stream));

        unsigned char* rgbi24_input = inbuf;
        size_t rgbi24_input_size = inbuf_size;

        unsigned char* rgbi24_input_device = NULL;
        CHECK_CUDA(cudaMalloc((void**)&rgbi24_input_device, rgbi24_input_size));
        nvjpegImage_t src;
        src.channel[0] = rgbi24_input_device;
        src.pitch[0] = width * 3;
        unsigned char* jpeg_output = malloc(rgbi24_input_size);

        clock_t total_processing_time = 0;
        for (int i = 0; i < iterations; i++) {
            clock_t t0 = clock();
            size_t jpeg_output_size = 0;
            cudaMemcpy(src.channel[0], rgbi24_input, rgbi24_input_size, cudaMemcpyHostToDevice);
            nvjpegEncodeImage(handle, encoder_state, encoder_params, &src, NVJPEG_INPUT_RGBI, width, height, stream);
            nvjpegEncodeRetrieveBitstream(handle, encoder_state, NULL, &jpeg_output_size, stream);
            nvjpegEncodeRetrieveBitstream(handle, encoder_state, jpeg_output, &jpeg_output_size, stream);
            cudaStreamSynchronize(stream);
            clock_t t1 = clock();
            total_processing_time += t1 - t0;
        }
        free(jpeg_output);
        fprintf(stderr, "Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        CHECK_CUDA(cudaFree(rgbi24_input_device));
        CHECK_NVJPEG(nvjpegEncoderParamsDestroy(encoder_params));
        CHECK_NVJPEG(nvjpegEncoderStateDestroy(encoder_state));
        CHECK_NVJPEG(nvjpegDestroy(handle));
    }

    /* === ENCODER SETUP === */
    cudaStream_t stream = 0;
    nvjpegHandle_t handle = NULL;
    nvjpegEncoderState_t encoder_state = NULL;
    nvjpegEncoderParams_t encoder_params = NULL;

    CHECK_NVJPEG(nvjpegCreateSimple(&handle));
    CHECK_NVJPEG(nvjpegEncoderStateCreateWithBackend(handle, &encoder_state, NVJPEG_ENC_BACKEND_DEFAULT, stream));
    CHECK_NVJPEG(nvjpegEncoderParamsCreate(handle, &encoder_params, stream));
    CHECK_NVJPEG(nvjpegEncoderParamsSetEncoding(encoder_params, NVJPEG_ENCODING_BASELINE_DCT, stream));
    CHECK_NVJPEG(nvjpegEncoderParamsSetQuality(encoder_params, quality, stream));
    CHECK_NVJPEG(nvjpegEncoderParamsSetOptimizedHuffman(encoder_params, 0, stream));
    CHECK_NVJPEG(nvjpegEncoderParamsSetSamplingFactors(encoder_params, subsampling_to_nvjpegsamp(subsampling), stream));

    unsigned char* rgbi24_input = inbuf;
    size_t rgbi24_input_size = inbuf_size;
    
    unsigned char* rgbi24_input_device = NULL;
    CHECK_CUDA(cudaMalloc((void**)&rgbi24_input_device, rgbi24_input_size));
    nvjpegImage_t src;
    src.channel[0] = rgbi24_input_device;
    src.pitch[0] = width * 3;

    size_t jpeg_output_size = 0;
    unsigned char* jpeg_output = NULL;

    /* === ENCODER TEST === */
    cudaMemcpy(src.channel[0], rgbi24_input, rgbi24_input_size, cudaMemcpyHostToDevice);
    CHECK_NVJPEG(nvjpegEncodeImage(handle, encoder_state, encoder_params, &src, NVJPEG_INPUT_RGBI, width, height, stream));
    CHECK_NVJPEG(nvjpegEncodeRetrieveBitstream(handle, encoder_state, NULL, &jpeg_output_size, stream));
    jpeg_output = malloc(jpeg_output_size);
    CHECK_NVJPEG(nvjpegEncodeRetrieveBitstream(handle, encoder_state, jpeg_output, &jpeg_output_size, stream));
    CHECK_CUDA(cudaStreamSynchronize(stream));

    /* === ENCODER CLEANUP === */
    img_destroy(rgbi24_input);
    CHECK_CUDA(cudaFree(rgbi24_input_device));
    CHECK_NVJPEG(nvjpegEncoderParamsDestroy(encoder_params));
    CHECK_NVJPEG(nvjpegEncoderStateDestroy(encoder_state));
    CHECK_NVJPEG(nvjpegDestroy(handle));

    /* === ENCODED IMAGE OUTPUT === */
    if (!strcmp(jpeg_output_path, "-"))
    {
        int err = write_img_to_stdout(jpeg_output, jpeg_output_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = write_img_to_path(jpeg_output_path, jpeg_output, jpeg_output_size);
        if (err)
        {
            return err;
        }
    }
    img_destroy(jpeg_output);

    return EXIT_SUCCESS;
}