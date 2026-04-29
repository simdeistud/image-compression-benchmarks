#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#define IMG_IO_IMPLEMENTATION
#include "img_io.h"

#include <jpeglib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Basic options:\n"
            "  --width <int>              Input image width (px)\n"
            "  --height <int>             Input image height (px)\n"
            "  --subsampling <int>        Selected subsampling [444|422|420|400]\n"
            "                             (400 = grayscale)\n"
            "  --quality <int>            Selected quality [0...100]\n"
            "  --dct_algorithm <str>      Selected DCT algorithm [int|fast|float]\n"
            "  --entropy_algorithm <str>  Selected entropy algorithm [huffman|arithmetic]\n"
            "  --restart_interval <int>   Selected restart interval [>=0]\n"
            "  --iterations <int>         Selected iterations (>0 if --benchmark)\n"
            "  --benchmark                Benchmark mode (flag)\n"
            "  --input <path>|-           Selected RGBI24 input [PATH|stdin]\n"
            "  --output <path>|-          Selected JPEG output [PATH|stdout]\n"
            "  --help                     Show this help and exit\n",
            prog);
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}

static J_DCT_METHOD get_dct(const char *dct_str)
{
    if (!dct_str) return JDCT_ISLOW; /* default */
    if (strcmp(dct_str, "int") == 0)   return JDCT_ISLOW;
    if (strcmp(dct_str, "fast") == 0)  return JDCT_IFAST;
    if (strcmp(dct_str, "float") == 0) return JDCT_FLOAT;
    return JDCT_ISLOW; /* fallback */
}

static int get_entropy(const char *entropy_str)
{
    if (!entropy_str) return 0; /* default: huffman */
    if (strcmp(entropy_str, "huffman") == 0)     return 0;
    if (strcmp(entropy_str, "arithmetic") == 0)  return 1;
    return 0; /* fallback */
}

/* Configure output colorspace + sampling factors.
   Call AFTER jpeg_set_defaults()/jpeg_set_quality()
   and BEFORE jpeg_start_compress().
*/
static void configure_colorspace_and_sampling(struct jpeg_compress_struct *cinfo, int subsampling)
{
    if (subsampling == 400) {
        /* 4:0:0 => grayscale (single component) */
        jpeg_set_colorspace(cinfo, JCS_GRAYSCALE);
        /* Only one component exists now */
        cinfo->comp_info[0].h_samp_factor = 1;
        cinfo->comp_info[0].v_samp_factor = 1;
        return;
    }

    /* Otherwise: standard JPEG uses YCbCr for 444/422/420 */
    jpeg_set_colorspace(cinfo, JCS_YCbCr);

    int hs = 1, vs = 1;
    switch (subsampling) {
        case 444: hs = 1; vs = 1; break; /* 4:4:4 */
        case 422: hs = 2; vs = 1; break; /* 4:2:2 */
        case 420: hs = 2; vs = 2; break; /* 4:2:0 */
        default:  hs = 1; vs = 1; break; /* default to 4:4:4 */
    }

    /* Component 0 = Y, 1 = Cb, 2 = Cr in JCS_YCbCr */
    cinfo->comp_info[0].h_samp_factor = hs;
    cinfo->comp_info[0].v_samp_factor = vs;

    cinfo->comp_info[1].h_samp_factor = 1;
    cinfo->comp_info[1].v_samp_factor = 1;

    cinfo->comp_info[2].h_samp_factor = 1;
    cinfo->comp_info[2].v_samp_factor = 1;
}

int main(int argc, char *argv[])
{
    int width = 0;
    int height = 0;
    int subsampling = 444;
    int quality = 75;
    char *dct_algorithm = NULL;
    char *entropy_algorithm = NULL;
    int restart_interval = 0;
    int iterations = 0;
    int benchmark = 0;
    char *rgbi24_input_path = NULL;
    char *jpeg_output_path = NULL;

    /* === ARGUMENT PARSING === */
    static const struct option long_opts[] = {
        {"width", required_argument, NULL, 1},
        {"height", required_argument, NULL, 2},
        {"subsampling", required_argument, NULL, 3},
        {"quality", required_argument, NULL, 4},
        {"dct_algorithm", required_argument, NULL, 5},
        {"entropy_algorithm", required_argument, NULL, 6},
        {"restart_interval", required_argument, NULL, 7},
        {"iterations", required_argument, NULL, 8},
        {"benchmark", no_argument, NULL, 9},
        {"input", required_argument, NULL, 10},
        {"output", required_argument, NULL, 11},
        {"help", no_argument, NULL, 12},
        {0, 0, 0, 0}};

    int opt, longidx;
    while ((opt = getopt_long(argc, argv, "", long_opts, &longidx)) != -1)
    {
        switch (opt)
        {
        case 1: if (parse_int(optarg, &width) != 0)  { fprintf(stderr, "Invalid --width: %s\n", optarg); usage(argv[0]); return EXIT_FAILURE; } break;
        case 2: if (parse_int(optarg, &height) != 0) { fprintf(stderr, "Invalid --height: %s\n", optarg); usage(argv[0]); return EXIT_FAILURE; } break;
        case 3: if (parse_int(optarg, &subsampling) != 0) { fprintf(stderr, "Invalid --subsampling: %s\n", optarg); usage(argv[0]); return EXIT_FAILURE; } break;
        case 4: if (parse_int(optarg, &quality) != 0) { fprintf(stderr, "Invalid --quality: %s\n", optarg); usage(argv[0]); return EXIT_FAILURE; } break;
        case 5: dct_algorithm = optarg; break;
        case 6: entropy_algorithm = optarg; break;
        case 7: if (parse_int(optarg, &restart_interval) != 0) { fprintf(stderr, "Invalid --restart_interval: %s\n", optarg); usage(argv[0]); return EXIT_FAILURE; } break;
        case 8: if (parse_int(optarg, &iterations) != 0) { fprintf(stderr, "Invalid --iterations: %s\n", optarg); usage(argv[0]); return EXIT_FAILURE; } break;
        case 9: benchmark = 1; break;
        case 10: rgbi24_input_path = optarg; break;
        case 11: jpeg_output_path = optarg; break;
        case 12: usage(argv[0]); return EXIT_SUCCESS;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!rgbi24_input_path || !jpeg_output_path || width <= 0 || height <= 0) {
        fprintf(stderr, "Missing/invalid required args.\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Validate subsampling */
    if (!(subsampling == 444 || subsampling == 422 || subsampling == 420 || subsampling == 400)) {
        fprintf(stderr, "Invalid --subsampling %d (allowed: 444, 422, 420, 400)\n", subsampling);
        return EXIT_FAILURE;
    }

    unsigned char *inbuf = NULL;
    size_t inbuf_size = 0;

    if (!strcmp(rgbi24_input_path, "-")) {
        int err = load_img_from_stdin(&inbuf, &inbuf_size);
        if (err) return err;
    } else {
        int err = load_img_from_path(rgbi24_input_path, &inbuf, &inbuf_size);
        if (err) return err;
    }

    /* Basic bounds check (input is RGBI24) */
    size_t needed = (size_t)width * (size_t)height * 3u;
    if (inbuf_size < needed) {
        fprintf(stderr, "Input buffer too small: got %zu, need %zu\n", inbuf_size, needed);
        img_destroy(inbuf);
        return EXIT_FAILURE;
    }

    if (benchmark) {
        /* === ENCODER BENCHMARK === */
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);

        JSAMPROW row_pointer[1];
        clock_t total_processing_time = 0;

        for (int i = 0; i < iterations; i++) {
            clock_t t0 = clock();

            cinfo.image_width = width;
            cinfo.image_height = height;
            cinfo.input_components = 3;
            cinfo.in_color_space = JCS_RGB;

            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, quality, TRUE);

            /* subsampling + colorspace */
            configure_colorspace_and_sampling(&cinfo, subsampling);

            cinfo.arith_code = get_entropy(entropy_algorithm);
            cinfo.dct_method = get_dct(dct_algorithm);
            cinfo.restart_interval = restart_interval; /* units: MCUs */
            cinfo.restart_in_rows  = 0;

            unsigned char *jpeg_output = NULL;
            unsigned long jpeg_output_size = 0;
            jpeg_mem_dest(&cinfo, &jpeg_output, &jpeg_output_size);

            jpeg_start_compress(&cinfo, TRUE);

            while (cinfo.next_scanline < cinfo.image_height) {
                row_pointer[0] = &inbuf[cinfo.next_scanline * cinfo.image_width * cinfo.input_components];
                jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }

            jpeg_finish_compress(&cinfo);

            clock_t t1 = clock();
            img_destroy(jpeg_output);

            total_processing_time += (t1 - t0);
        }

        jpeg_destroy_compress(&cinfo);
        fprintf(stderr, "Total processing time (seconds): %f\n",
                ((double)total_processing_time) / CLOCKS_PER_SEC);
    }

    /* === ENCODER CREATION === */
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    /* === ENCODER SETUP === */
    JSAMPROW row_pointer[1];

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    /* subsampling + colorspace */
    configure_colorspace_and_sampling(&cinfo, subsampling);

    cinfo.arith_code = get_entropy(entropy_algorithm);
    cinfo.dct_method = get_dct(dct_algorithm);
    cinfo.restart_interval = restart_interval;
    cinfo.restart_in_rows  = 0;

    unsigned char *jpeg_output = NULL;
    unsigned long jpeg_output_size = 0;
    jpeg_mem_dest(&cinfo, &jpeg_output, &jpeg_output_size);

    jpeg_start_compress(&cinfo, TRUE);

    /* === ENCODER WRITE === */
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &inbuf[cinfo.next_scanline * cinfo.image_width * cinfo.input_components];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    img_destroy(inbuf);

    /* === ENCODED IMAGE OUTPUT === */
    if (!strcmp(jpeg_output_path, "-")) {
        int err = write_img_to_stdout(jpeg_output, (size_t)jpeg_output_size);
        if (err) { img_destroy(jpeg_output); return err; }
    } else {
        int err = write_img_to_path(jpeg_output_path, jpeg_output, (size_t)jpeg_output_size);
        if (err) { img_destroy(jpeg_output); return err; }
    }

    img_destroy(jpeg_output);
    return EXIT_SUCCESS;
}
