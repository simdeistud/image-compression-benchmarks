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
    if (s != NVjpeg2k_STATUS_SUCCESS) {                                           \
        fprintf(stderr, "nvjpeg2k error: %d at %s:%d\n", (int)s,                  \
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
        "  --input <path>|-             Selected jpeg2k input [PATH|stdin]\n"
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

nvjpeg2kChromaSubsampling_t subsampling_to_nvjpeg2ksamp(const int subsampling)
{
    switch (subsampling)
    {
    case 444: return NVjpeg2k_CSS_444;
    case 422: return NVjpeg2k_CSS_422;
    case 420: return NVjpeg2k_CSS_420;
    default: return NVjpeg2k_CSS_UNKNOWN;
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
    char* jpeg2k_output_path = NULL;

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
            jpeg2k_output_path = optarg;
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

    if (benchmark) {
        /* === ENCODER BENCHMARK === */
        
        clock_t total_processing_time = 0;
        for (int i = 0; i < iterations; i++) {
            clock_t t0 = clock();
            
            clock_t t1 = clock();
            total_processing_time += t1 - t0;
        }
        
        printf("Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        printf("Average processing time per iteration (milliseconds):%f\n", ((double)total_processing_time) / iterations / CLOCKS_PER_SEC * 1000);
        printf("Average frames per second:%f\n", iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        printf("Average megapixels per second:%f\n", (width * height) / (double)1000000 * iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        img_destroy(rgbi24_input);
    }

    /* === ENCODER SETUP === */
    cudaStream_t stream = 0;
    nvjpeg2kEncoder_t enc_handle = NULL;
    nvjpeg2kEncodeState_t enc_state = NULL;
    nvjpeg2kEncodeParams_t enc_params = NULL;

    CHECK_NVJ2K(nvjpeg2kEncoderCreateSimple(&enc_handle));
    CHECK_NVJ2K(nvjpeg2kEncodeStateCreate(enc_handle, &enc_state));
    CHECK_NVJ2K(nvjpeg2kEncodeParamsCreate(&enc_params));
    CHECK_NVJ2K(nvjpeg2kEncodeParamsSetInputFormat(enc_params, NVJPEG2K_FORMAT_INTERLEAVED));

    nvjpeg2kImageComponentInfo_t comp_info;
    comp_info.component_width  = width;
    comp_info.component_height = height;
    comp_info.precision        = 8;
    comp_info.sgn              = 0;

    nvjpeg2kEncodeConfig_t enc_config;
    enc_config.stream_type     = NVJPEG2K_STREAM_JP2;
    enc_config.color_space     = NVJPEG2K_COLORSPACE_SRGB;
    enc_config.image_width     = width;
    enc_config.image_height    = height;
    enc_config.num_components  = 3;
    enc_config.image_comp_info[0] = comp_info;
    enc_config.code_block_w    = 64;
    enc_config.code_block_h    = 64;
    enc_config.irreversible    = 1;
    enc_config.mct_mode        = 1;
    enc_config.prog_order      = NVJPEG2K_LRCP;
    enc_config.num_resolutions = 6;
    enc_config.rsiz            = NVJPEG2K_RSIZ_HT;
    enc_config.encode_modes    = NVJPEG2K_MODE_HT;

    CHECK_NVJ2K(nvjpeg2kEncodeParamsSetEncodeConfig(enc_params, &enc_config));
    CHECK_NVJ2K(nvjpeg2kEncodeParamsSpecifyQuality(
        enc_params, NVJPEG2K_QUALITY_TYPE_Q_FACTOR, (double )quality));

    unsigned char* rgbi24_input = NULL;
    size_t rgbi24_input_size = 0;
    if (!strcmp(rgbi24_input_path, "-"))
    {
        int err = load_img_from_stdin(&rgbi24_input, &rgbi24_input_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = load_img_from_path(rgbi24_input_path, &rgbi24_input, &rgbi24_input_size);
        if (err)
        {
            return err;
        }
    }
    unsigned char* rgbi24_input_device = NULL;
    CHECK_CUDA(cudaMalloc((void**)&rgbi24_input_device, rgbi24_input_size));
    nvjpeg2kImage_t src;
    src.channel[0] = rgbi24_input_device;
    src.pitch[0] = width * 3;

    size_t jpeg2k_output_size = 0;
    unsigned char* jpeg2k_output = NULL;

    /* === ENCODER TEST === */
    cudaMemcpy(src.channel[0], rgbi24_input, rgbi24_input_size, cudaMemcpyHostToDevice);
    CHECK_NVjpeg2k(nvjpeg2kEncodeImage(handle, encoder_state, encoder_params, &src, NVjpeg2k_INPUT_RGBI, width, height, stream));
    CHECK_NVjpeg2k(nvjpeg2kEncodeRetrieveBitstream(handle, encoder_state, NULL, &jpeg2k_output_size, stream));
    jpeg2k_output = malloc(jpeg2k_output_size);
    CHECK_NVjpeg2k(nvjpeg2kEncodeRetrieveBitstream(handle, encoder_state, jpeg2k_output, &jpeg2k_output_size, stream));
    CHECK_CUDA(cudaStreamSynchronize(stream));

    /* === ENCODER CLEANUP === */
    img_destroy(rgbi24_input);
    CHECK_CUDA(cudaFree(rgbi24_input_device));
    CHECK_NVjpeg2k(nvjpeg2kEncoderParamsDestroy(encoder_params));
    CHECK_NVjpeg2k(nvjpeg2kEncoderStateDestroy(encoder_state));
    CHECK_NVjpeg2k(nvjpeg2kDestroy(handle));

    /* === ENCODED IMAGE OUTPUT === */
    if (!strcmp(jpeg2k_output_path, "-"))
    {
        int err = write_img_to_stdout(jpeg2k_output, jpeg2k_output_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = write_img_to_path(jpeg2k_output_path, jpeg2k_output, jpeg2k_output_size);
        if (err)
        {
            return err;
        }
    }
    img_destroy(jpeg2k_output);

    return EXIT_SUCCESS;
}