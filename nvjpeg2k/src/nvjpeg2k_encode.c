#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#define IMG_IO_IMPLEMENTATION
#include "img_io.h"

#include <cuda_runtime_api.h>
#include <nvjpeg2k.h>

#define CHECK_CUDA(cmd)                                                         \
do {                                                                            \
    cudaError_t e = (cmd);                                                      \
    if (e != cudaSuccess) {                                                     \
        fprintf(stderr, "CUDA error: %s at %s:%d\n", cudaGetErrorString(e),     \
                __FILE__, __LINE__);                                            \
        return 1;                                                               \
    }                                                                           \
} while (0)

#define CHECK_NVJ2K(cmd)                                                        \
do {                                                                            \
    nvjpeg2kStatus_t s = (cmd);                                                 \
    if (s != NVJPEG2K_STATUS_SUCCESS) {                                         \
        fprintf(stderr, "nvjpeg2k error: %d at %s:%d\n", (int)s,                \
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
        "  --mode <str>                 Encoding mode [lossless|lossy|lossless-ht|lossy-ht]\n"
        "  --quality <int>              Quality [0..100] (lossy* only)\n"
        "  --iterations <int>           Iterations (>0 if --benchmark)\n"
        "  --benchmark                  Benchmark mode (flag)\n"
        "  --input <path>|-             RGBI24 input [PATH|stdin]\n"
        "  --output <path>|-            JP2 output [PATH|stdout]\n"
        "  --help                       Show this help and exit\n",
        prog);
}

static int parse_int(const char* s, int* out)
{
    char* end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX) return -1;
    *out = (int)v;
    return 0;
}

typedef struct {
    int lossy;  /* 1 => irreversible, 0 => reversible */
    int ht;     /* 1 => HT profile */
} mode_cfg_t;

static int parse_mode_cfg(const char* s, mode_cfg_t* out)
{
    if (!s || !out) return -1;
    if (strcmp(s, "lossless") == 0)        { out->lossy = 0; out->ht = 0; return 0; }
    if (strcmp(s, "lossy") == 0)           { out->lossy = 1; out->ht = 0; return 0; }
    if (strcmp(s, "lossless-ht") == 0)     { out->lossy = 0; out->ht = 1; return 0; }
    if (strcmp(s, "lossy-ht") == 0)        { out->lossy = 1; out->ht = 1; return 0; }
    return -1;
}

int main(int argc, char* argv[])
{
    int width = 0, height = 0;
    char* mode_str = NULL;
    int quality = 0;

    int iterations = 0, benchmark = 0;
    char* rgbi24_input_path = NULL;
    char* jpeg2k_output_path = NULL;

    static const struct option long_opts[] = {
        {"width", required_argument, NULL, 1},
        {"height", required_argument, NULL, 2},
        {"mode", required_argument, NULL, 3},
        {"quality", required_argument, NULL, 4},
        {"iterations", required_argument, NULL, 5},
        {"benchmark", no_argument, NULL, 6},
        {"input", required_argument, NULL, 7},
        {"output", required_argument, NULL, 8},
        {"help", no_argument, NULL, 9},
        {0,0,0,0}
    };

    int opt, longidx;
    while ((opt = getopt_long(argc, argv, "", long_opts, &longidx)) != -1) {
        switch (opt) {
        case 1: if (parse_int(optarg, &width) != 0)  { usage(argv[0]); return 1; } break;
        case 2: if (parse_int(optarg, &height) != 0) { usage(argv[0]); return 1; } break;
        case 3: mode_str = optarg; break;
        case 4: if (parse_int(optarg, &quality) != 0) { usage(argv[0]); return 1; } break;
        case 5: if (parse_int(optarg, &iterations) != 0) { usage(argv[0]); return 1; } break;
        case 6: benchmark = 1; break;
        case 7: rgbi24_input_path = optarg; break;
        case 8: jpeg2k_output_path = optarg; break;
        case 9: usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }

    if (!rgbi24_input_path || !jpeg2k_output_path || width <= 0 || height <= 0) {
        usage(argv[0]);
        return 1;
    }

    mode_cfg_t mc = { .lossy = 1, .ht = 0 };
    if (mode_str && parse_mode_cfg(mode_str, &mc) != 0) {
        fprintf(stderr, "Invalid mode (lossless|lossy|lossless-ht|lossy-ht)\n");
        return 1;
    }

    /* load RGBI24 host */
    unsigned char* inbuf = NULL;
    size_t inbuf_size = 0;
    if (!strcmp(rgbi24_input_path, "-")) {
        int err = load_img_from_stdin(&inbuf, &inbuf_size);
        if (err) return err;
    } else {
        int err = load_img_from_path(rgbi24_input_path, &inbuf, &inbuf_size);
        if (err) return err;
    }

    cudaStream_t stream = 0;

    /* nvjpeg2k setup */
    nvjpeg2kEncoder_t enc_handle = NULL;
    nvjpeg2kEncodeState_t enc_state = NULL;
    nvjpeg2kEncodeParams_t enc_params = NULL;

    CHECK_NVJ2K(nvjpeg2kEncoderCreateSimple(&enc_handle));
    CHECK_NVJ2K(nvjpeg2kEncodeStateCreate(enc_handle, &enc_state));
    CHECK_NVJ2K(nvjpeg2kEncodeParamsCreate(&enc_params));

    /* INTERLEAVED input (RGBI): allowed only if component dimensions are the same. 【1-89bdaf】 */
    CHECK_NVJ2K(nvjpeg2kEncodeParamsSetInputFormat(enc_params, NVJPEG2K_FORMAT_INTERLEAVED));

    /* device RGBI */
    unsigned char *rgbi24_input = inbuf;
    size_t rgbi24_input_size = inbuf_size;

    unsigned char* rgbi24_input_device = NULL;
    CHECK_CUDA(cudaMalloc((void**)&rgbi24_input_device, rgbi24_input_size));

    /* encode config */
    nvjpeg2kImageComponentInfo_t comp_info[3];
    memset(comp_info, 0, sizeof(comp_info));
    for (int c = 0; c < 3; c++) {
        comp_info[c].component_width  = width;
        comp_info[c].component_height = height;
        comp_info[c].precision = 8;
        comp_info[c].sgn = 0;
    }

    nvjpeg2kEncodeConfig_t enc_config;
    memset(&enc_config, 0, sizeof(enc_config));

    enc_config.stream_type = NVJPEG2K_STREAM_JP2;
    enc_config.color_space = NVJPEG2K_COLORSPACE_SRGB;
    enc_config.mct_mode = 1;
    enc_config.image_width = width;
    enc_config.image_height = height;
    enc_config.num_components = 3;
    enc_config.image_comp_info = comp_info;

    /* mode -> reversible/irreversible */
    enc_config.irreversible = mc.lossy ? 1u : 0u;

    /* common settings */
    enc_config.code_block_w = 64;
    enc_config.code_block_h = 64;
    enc_config.prog_order = NVJPEG2K_LRCP;
    enc_config.num_resolutions = 6;

    /* HT is selectable (only set when requested) */
    if (mc.ht) {
        enc_config.rsiz = NVJPEG2K_RSIZ_HT;
        enc_config.encode_modes = NVJPEG2K_MODE_HT;
    }

    CHECK_NVJ2K(nvjpeg2kEncodeParamsSetEncodeConfig(enc_params, &enc_config));

    /* lossy -> Q factor; lossless -> must skip (invalid for reversible). 【1-89bdaf】 */
    if (mc.lossy) {
        CHECK_NVJ2K(nvjpeg2kEncodeParamsSpecifyQuality(
            enc_params, NVJPEG2K_QUALITY_TYPE_Q_FACTOR, (double)quality));
    }

    /* input image (INTERLEAVED uses pixel_data[0] + pitch[0]) */
    nvjpeg2kImage_t src;
    memset(&src, 0, sizeof(src));
    src.pixel_type = NVJPEG2K_UINT8;
    src.num_components = 3;

    unsigned char* ptrs0[3] = { rgbi24_input_device, NULL, NULL };
    size_t pitch0[3] = { (size_t)width * 3, 0, 0 };
    src.pixel_data = (void**)ptrs0;
    src.pitch_in_bytes = pitch0;

    unsigned char* jpeg2k_output = NULL;
    size_t jpeg2k_output_size = 0;

    if (benchmark) {

        clock_t total = 0;
        for (int i = 0; i < iterations; i++) {
            clock_t t0 = clock();
            cudaMemcpy(rgbi24_input_device, rgbi24_input, rgbi24_input_size, cudaMemcpyHostToDevice);
            nvjpeg2kEncode(enc_handle, enc_state, enc_params, &src, stream);
            nvjpeg2kEncodeRetrieveBitstream(enc_handle, enc_state, NULL, &jpeg2k_output_size, stream);
            jpeg2k_output = (unsigned char*)malloc(jpeg2k_output_size);
            nvjpeg2kEncodeRetrieveBitstream(enc_handle, enc_state, jpeg2k_output, &jpeg2k_output_size, stream);
            cudaStreamSynchronize(stream); /* required when compressed_data != NULL 【2-382b04】 */
            clock_t t1 = clock();
            total += (t1 - t0);
            free(jpeg2k_output);
        }
        fprintf(stderr, "Total processing time (seconds): %f\n", (double)total / CLOCKS_PER_SEC);
    }

    /* Encode once for real output */
    CHECK_CUDA(cudaMemcpy(rgbi24_input_device, rgbi24_input, rgbi24_input_size, cudaMemcpyHostToDevice));
    CHECK_NVJ2K(nvjpeg2kEncode(enc_handle, enc_state, enc_params, &src, stream));
    CHECK_NVJ2K(nvjpeg2kEncodeRetrieveBitstream(enc_handle, enc_state, NULL, &jpeg2k_output_size, stream));
    jpeg2k_output = (unsigned char*)malloc(jpeg2k_output_size);
    CHECK_NVJ2K(nvjpeg2kEncodeRetrieveBitstream(enc_handle, enc_state, jpeg2k_output, &jpeg2k_output_size, stream));
    CHECK_CUDA(cudaStreamSynchronize(stream)); /* required when compressed_data != NULL 【2-382b04】 */

    /* cleanup */
    img_destroy(inbuf);
    CHECK_CUDA(cudaFree(rgbi24_input_device));
    CHECK_NVJ2K(nvjpeg2kEncodeParamsDestroy(enc_params));
    CHECK_NVJ2K(nvjpeg2kEncodeStateDestroy(enc_state));
    CHECK_NVJ2K(nvjpeg2kEncoderDestroy(enc_handle));

    /* output */
    if (!strcmp(jpeg2k_output_path, "-")) {
        int err = write_img_to_stdout(jpeg2k_output, jpeg2k_output_size);
        if (err) return err;
    } else {
        int err = write_img_to_path(jpeg2k_output_path, jpeg2k_output, jpeg2k_output_size);
        if (err) return err;
    }
    img_destroy(jpeg2k_output);
    return 0;
}