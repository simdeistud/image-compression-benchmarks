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
            "  --subsampling <int>        Selected subsampling [444|422|420]\n"
            "  --quality <int>            Selected quality [0...100]\n"
            "  --dct_algorithm <str>      Selected DCT algorithm [int|fast|float]\n"
            "  --entropy_algorithm <str>  Selected entropy algorithm [huffman|arithmetic]\n"
            "  --restart_interval <int>   Selected restart interval [>=0]\n"
            "  --iterations <int>         Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                Benchmark mode (flag)\n"
            "  --input <path>|-           Selected JPEG input [PATH|stdin]\n"
            "  --output <path>|-          Selected RGBI24 output [PATH|stdout]\n"
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

int subsampling_to_vsamp(const int subsampling)
{
    switch (subsampling)
    {
    case 444:
    case 422: return 1;
    case 420: return 2;
    default: return 1;
    }
}

int subsampling_to_hsamp(const int subsampling)
{
    switch (subsampling)
    {
    case 444: return 1;
    case 422:
    case 420: return 2;
    default: return 0;
    }
}

J_DCT_METHOD get_dct(const char *dct_str)
{
    if (strcmp(dct_str, "int") == 0)
        return JDCT_ISLOW;
    if (strcmp(dct_str, "fast") == 0)
        return JDCT_IFAST;
    if (strcmp(dct_str, "float") == 0)
        return JDCT_FLOAT;
    return -1;
}

int get_entropy(const char *entropy_str)
{
    if (strcmp(entropy_str, "huffman") == 0)
        return 0;
    if (strcmp(entropy_str, "arithmetic") == 0)
        return 1;
    return -1;
}

int main(int argc, char *argv[])
{
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int quality = 0;
    char *dct_algorithm = NULL;
    char *entropy_algorithm = NULL;
    int restart_interval = 0;
    int iterations = 0;
    int benchmark = 0;
    char *rgbi24_input_path = NULL;
    char *jpeg_output_path = NULL;

    /* === ARGUMENT PARSING === */

    /* Long-only; provide no short options (optstring = "") */
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
        case 5: /* --dct_algorithm */
            dct_algorithm = optarg;
            break;
        case 6: /* --entropy_algorithm */
            entropy_algorithm = optarg;
            break;
        case 7:
        { /* --restart_interval */
            if (parse_int(optarg, &restart_interval) != 0)
            {
                fprintf(stderr, "Invalid --restart_interval value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 8:
        { /* --iterations */
            if (parse_int(optarg, &iterations) != 0)
            {
                fprintf(stderr, "Invalid --iterations value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 9: /* --benchmark */
            benchmark = 1;
            break;
        case 10: /* --input */
            rgbi24_input_path = optarg;
            break;
        case 11: /* --output */
            jpeg_output_path = optarg;
            break;
        case 12: /* --help */
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

    unsigned char *inbuf = NULL;
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

    if (benchmark){
        /* === ENCODER BENCHMARK === */
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        JSAMPLE *rgbi24_input = inbuf;
        size_t rgbi24_input_size = inbuf_size;
        JSAMPROW row_pointer[1];
        
        clock_t total_processing_time = 0;
        for (int i = 0; i < iterations; i++){
            clock_t t0 = clock();
            cinfo.image_width = width;
            cinfo.image_height = height;
            cinfo.input_components = 3;
            cinfo.in_color_space = JCS_RGB;
            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, quality, TRUE);
            jpeg_set_colorspace(&cinfo, JCS_RGB); /* Output colorspace, we choose RGB */
            cinfo.comp_info[0].v_samp_factor = subsampling_to_vsamp(subsampling);
            cinfo.comp_info[0].h_samp_factor = subsampling_to_hsamp(subsampling);
            cinfo.arith_code = get_entropy(entropy_algorithm);
            cinfo.dct_method = get_dct(dct_algorithm);
            cinfo.restart_interval = restart_interval;
            size_t jpeg_output_size = 0;
            JSAMPLE *jpeg_output = NULL;
            jpeg_mem_dest(&cinfo, &jpeg_output, &jpeg_output_size);
            jpeg_start_compress(&cinfo, TRUE);
            while (cinfo.next_scanline < cinfo.image_height)
            {
                row_pointer[0] = &rgbi24_input[cinfo.next_scanline * cinfo.image_width * cinfo.input_components];
                jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }
            jpeg_finish_compress(&cinfo);
            clock_t t1 = clock();
            img_destroy(jpeg_output);
            total_processing_time += t1 - t0;
        }
        jpeg_destroy_compress(&cinfo);
        fprintf(stderr, "Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
    }

    /* === ENCODER CREATION === */
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    /* === ENCODER SETUP === */
    JSAMPLE *rgbi24_input = inbuf;
    size_t rgbi24_input_size = inbuf_size;
    JSAMPROW row_pointer[1];
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_set_colorspace(&cinfo, JCS_RGB); /* Output colorspace, we choose RGB */
    cinfo.comp_info[0].v_samp_factor = subsampling_to_vsamp(subsampling);
    cinfo.comp_info[0].h_samp_factor = subsampling_to_hsamp(subsampling);
    cinfo.arith_code = get_entropy(entropy_algorithm);
    cinfo.dct_method = get_dct(dct_algorithm);
    cinfo.restart_interval = restart_interval;
    size_t jpeg_output_size = 0;
    JSAMPLE *jpeg_output = NULL;
    jpeg_mem_dest(&cinfo, &jpeg_output, &jpeg_output_size);
    jpeg_start_compress(&cinfo, TRUE);

    /* === ENCODER TEST === */
    while (cinfo.next_scanline < cinfo.image_height)
    {
        row_pointer[0] = &rgbi24_input[cinfo.next_scanline * cinfo.image_width * cinfo.input_components];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    

    /* === ENCODER RESET === */
    jpeg_finish_compress(&cinfo);

    /* === ENCODER CLEANUP === */
    jpeg_destroy_compress(&cinfo);
    img_destroy(rgbi24_input);

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