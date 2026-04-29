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

static void usage(const char* prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Basic options:\n"
        "  --dct_algorithm <str>      Selected iDCT algorithm [int|fast|float]\n"
        "  --iterations <int>         Selected iterations [>0 if --benchmark]\n"
        "  --benchmark                Benchmark mode (flag)\n"
        "  --input <path>|-           Selected JPEG [PATH|stdin]\n"
        "  --output <path>|-          Selected RGBI24 [PATH|stdout]\n"
        "  --help                     Show this help and exit\n",
        prog);
}

static int parse_int(const char* s, int* out)
{
    char* end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}

static J_DCT_METHOD get_dct(const char* dct_str)
{
    if (!dct_str) return JDCT_ISLOW;
    if (strcmp(dct_str, "int") == 0)   return JDCT_ISLOW;
    if (strcmp(dct_str, "fast") == 0)  return JDCT_IFAST;
    if (strcmp(dct_str, "float") == 0) return JDCT_FLOAT;
    return JDCT_ISLOW; /* safe fallback */
}

static int mul_overflow_size_t(size_t a, size_t b, size_t* out)
{
    if (a == 0 || b == 0) { *out = 0; return 0; }
    if (a > SIZE_MAX / b) return 1;
    *out = a * b;
    return 0;
}

int main(int argc, char* argv[])
{
    char* dct_algorithm = NULL;
    int iterations = 0;
    int benchmark = 0;
    char* jpeg_input_path = NULL;
    char* rgbi24_output_path = NULL;

    static const struct option long_opts[] = {
        {"dct_algorithm", required_argument, NULL, 1},
        {"iterations", required_argument, NULL, 2},
        {"benchmark", no_argument, NULL, 3},
        {"input", required_argument, NULL, 4},
        {"output", required_argument, NULL, 5},
        {"help", no_argument, NULL, 6},
        {0, 0, 0, 0}
    };

    int opt, longidx;
    while ((opt = getopt_long(argc, argv, "", long_opts, &longidx)) != -1)
    {
        switch (opt)
        {
        case 1: dct_algorithm = optarg; break;
        case 2:
            if (parse_int(optarg, &iterations) != 0) {
                fprintf(stderr, "Invalid --iterations value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            break;
        case 3: benchmark = 1; break;
        case 4: jpeg_input_path = optarg; break;
        case 5: rgbi24_output_path = optarg; break;
        case 6: usage(argv[0]); return EXIT_SUCCESS;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!jpeg_input_path || !rgbi24_output_path) {
        fprintf(stderr, "Missing --input/--output\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    unsigned char* inbuf = NULL;
    size_t inbuf_size = 0;

    if (!strcmp(jpeg_input_path, "-")) {
        int err = load_img_from_stdin(&inbuf, &inbuf_size);
        if (err) return err;
    } else {
        int err = load_img_from_path(jpeg_input_path, &inbuf, &inbuf_size);
        if (err) return err;
    }

    /* jpeg_mem_src() expects unsigned long */
    unsigned long jpeg_input_size_ul = (unsigned long)inbuf_size;

    if (benchmark) {
        /* === DECODER BENCHMARK (includes malloc/free in timed region) === */
        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);

        JSAMPROW row_pointer[1];
        clock_t total_processing_time = 0;

        for (int i = 0; i < iterations; i++) {
            clock_t t0 = clock();

            jpeg_mem_src(&cinfo, inbuf, jpeg_input_size_ul);

            if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
                fprintf(stderr, "Error: Failed to read JPEG header\n");
                jpeg_destroy_decompress(&cinfo);
                img_destroy(inbuf);
                return EXIT_FAILURE;
            }

            cinfo.dct_method = get_dct(dct_algorithm);

            /* Force RGBI24 output always (expands grayscale -> RGB) */
            cinfo.out_color_space = JCS_RGB;

            jpeg_start_decompress(&cinfo);

            size_t row_stride = (size_t)cinfo.output_width * (size_t)cinfo.output_components;
            size_t out_size = 0;
            if (mul_overflow_size_t(row_stride, (size_t)cinfo.output_height, &out_size)) {
                fprintf(stderr, "Error: output size overflow\n");
                jpeg_finish_decompress(&cinfo);
                jpeg_destroy_decompress(&cinfo);
                img_destroy(inbuf);
                return EXIT_FAILURE;
            }

            unsigned char* rgbi24_output = (unsigned char*)malloc(out_size);
            if (!rgbi24_output) {
                fprintf(stderr, "Error: malloc(%zu) failed\n", out_size);
                jpeg_finish_decompress(&cinfo);
                jpeg_destroy_decompress(&cinfo);
                img_destroy(inbuf);
                return EXIT_FAILURE;
            }

            while (cinfo.output_scanline < cinfo.output_height) {
                row_pointer[0] = &rgbi24_output[cinfo.output_scanline * row_stride];
                jpeg_read_scanlines(&cinfo, row_pointer, 1);
            }

            jpeg_finish_decompress(&cinfo);

            free(rgbi24_output);

            clock_t t1 = clock();
            total_processing_time += (t1 - t0);
        }

        fprintf(stderr, "Total processing time (seconds): %f\n",
                ((double)total_processing_time) / CLOCKS_PER_SEC);

        jpeg_destroy_decompress(&cinfo);
    }

    /* === DECODER (single run) === */
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, inbuf, jpeg_input_size_ul);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        fprintf(stderr, "Error: Failed to read JPEG header from: %s\n", jpeg_input_path);
        jpeg_destroy_decompress(&cinfo);
        img_destroy(inbuf);
        return EXIT_FAILURE;
    }

    cinfo.dct_method = get_dct(dct_algorithm);

    /* Force RGBI24 output always */
    cinfo.out_color_space = JCS_RGB;

    jpeg_start_decompress(&cinfo);

    size_t row_stride = (size_t)cinfo.output_width * (size_t)cinfo.output_components;
    size_t rgbi24_output_size = 0;
    if (mul_overflow_size_t(row_stride, (size_t)cinfo.output_height, &rgbi24_output_size)) {
        fprintf(stderr, "Error: output size overflow\n");
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        img_destroy(inbuf);
        return EXIT_FAILURE;
    }

    unsigned char* rgbi24_output = (unsigned char*)malloc(rgbi24_output_size);
    if (!rgbi24_output) {
        fprintf(stderr, "Error: malloc(%zu) failed\n", rgbi24_output_size);
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        img_destroy(inbuf);
        return EXIT_FAILURE;
    }

    JSAMPROW row_pointer[1];
    while (cinfo.output_scanline < cinfo.output_height) {
        row_pointer[0] = &rgbi24_output[cinfo.output_scanline * row_stride];
        jpeg_read_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    img_destroy(inbuf);

    /* === OUTPUT === */
    if (!strcmp(rgbi24_output_path, "-")) {
        int err = write_img_to_stdout(rgbi24_output, rgbi24_output_size);
        if (err) { free(rgbi24_output); return err; }
    } else {
        int err = write_img_to_path(rgbi24_output_path, rgbi24_output, rgbi24_output_size);
        if (err) { free(rgbi24_output); return err; }
    }

    free(rgbi24_output);
    return EXIT_SUCCESS;
}
