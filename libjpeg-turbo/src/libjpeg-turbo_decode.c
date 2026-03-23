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
            "  --dct_algorithm <str>      Selected iDCT algorithm [int|fast|float]\n"
            "  --iterations <int>         Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                Benchmark mode (flag)\n"
            "  --input <path>|-           Selected JPEG [PATH|stdin]\n"
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

int main(int argc, char *argv[])
{

    char *dct_algorithm = NULL;
    int iterations = 0;
    int benchmark = 0;
    char *jpeg_input_path = NULL;
    char *rgbi24_output_path = NULL;

    /* === ARGUMENT PARSING === */

    /* Long-only; provide no short options (optstring = "") */
    static const struct option long_opts[] = {
        {"dct_algorithm", required_argument, NULL, 1},
        {"iterations", required_argument, NULL, 2},
        {"benchmark", no_argument, NULL, 3},
        {"input", required_argument, NULL, 4},
        {"output", required_argument, NULL, 5},
        {"help", no_argument, NULL, 6},
        {0, 0, 0, 0}};

    int opt, longidx;
    /* Reset getopt state if needed (when embedding): optind = 1; */
    while ((opt = getopt_long(argc, argv, "", long_opts, &longidx)) != -1)
    {
        switch (opt)
        {
        case 1: /* --dct_algorithm */
            dct_algorithm = optarg;
            break;
        case 2:
        { /* --iterations */
            if (parse_int(optarg, &iterations) != 0)
            {
                fprintf(stderr, "Invalid --iterations value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 3: /* --benchmark */
            benchmark = 1;
            break;
        case 4: /* --input */
            jpeg_input_path = optarg;
            break;
        case 5: /* --output */
            rgbi24_output_path = optarg;
            break;
        case 6: /* --help */
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
        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        JSAMPLE *jpeg_input = NULL;
        size_t jpeg_input_size = 0;
        JSAMPROW row_pointer[1];
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
        clock_t total_processing_time = 0;
        int width = 0, height = 0;
        for (int i = 0; i < iterations; i++)
        {
            clock_t t0 = clock();
            jpeg_mem_src(&cinfo, jpeg_input, jpeg_input_size);
            jpeg_read_header(&cinfo, TRUE);
            cinfo.dct_method = get_dct(dct_algorithm);
            jpeg_start_decompress(&cinfo);
            width = cinfo.output_width;
            height = cinfo.output_height;
            size_t rgbi24_output_size = width * height * cinfo.output_components;
            JSAMPLE *rgbi24_output = (unsigned char *)malloc(rgbi24_output_size);
            while (cinfo.output_scanline < cinfo.output_height)
            {
                row_pointer[0] = &rgbi24_output[cinfo.output_scanline * cinfo.output_width * cinfo.output_components];
                jpeg_read_scanlines(&cinfo, row_pointer, 1);
            }
            jpeg_finish_decompress(&cinfo);
            clock_t t1 = clock();
            img_destroy(rgbi24_output);
            total_processing_time += t1 - t0;
        }
        printf("Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        printf("Average processing time per iteration (milliseconds):%f\n", ((double)total_processing_time) / iterations / CLOCKS_PER_SEC * 1000);
        printf("Average frames per second:%f\n", iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        printf("Average megapixels per second:%f\n", (cinfo.output_width * cinfo.output_height) / (double)1000000 * iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        jpeg_destroy_decompress(&cinfo);
        img_destroy(jpeg_input);
    }

    /* === DECODER CREATION === */
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    /* === DECODER SETUP === */
    JSAMPLE *jpeg_input = NULL;
    size_t jpeg_input_size = 0;
    JSAMPROW row_pointer[1];
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
    jpeg_mem_src(&cinfo, jpeg_input, jpeg_input_size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
    { /* Reading the JPEG header is mandatory before starting the decompression */
        fprintf(stderr, "Error: Failed to read JPEG header from: %s\n", jpeg_input_path);
        return 1;
    }
    cinfo.dct_method = get_dct(dct_algorithm);
    jpeg_start_decompress(&cinfo);
    size_t rgbi24_output_size = cinfo.output_width * cinfo.output_height * cinfo.output_components;
    JSAMPLE *rgbi24_output = (unsigned char *)malloc(rgbi24_output_size);

    /* === DECODER TEST === */
    while (cinfo.output_scanline < cinfo.output_height)
    {
        row_pointer[0] = &rgbi24_output[cinfo.output_scanline * cinfo.output_width * cinfo.output_components];
        jpeg_read_scanlines(&cinfo, row_pointer, 1);
    }

    /* === DECODER RESET === */
    jpeg_finish_decompress(&cinfo);

    /* === DECODER CLEANUP === */
    jpeg_destroy_decompress(&cinfo);
    img_destroy(jpeg_input);

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
    img_destroy(rgbi24_output);

    return EXIT_SUCCESS;
}