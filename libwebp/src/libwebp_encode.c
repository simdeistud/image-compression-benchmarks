#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#define IMG_IO_IMPLEMENTATION
#include "img_io.h"
#include <webp/encode.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Options:\n"
            "  --width <int>                Input image width (px)\n"
            "  --height <int>               Input image height (px)\n"
            "  --quality <float>            Selected quality [0...100]\n"
            "  --iterations <int>           Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                  Benchmark mode (flag)\n"
            "  --input <path>|-             Selected webp input [PATH|stdin]\n"
            "  --output <path>|-            Selected RGBI24 output [PATH|stdout]\n"
            "  --help                       Show this help and exit\n",
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

static float parse_float(const char *s, float *out)
{
    char *end = NULL;
    errno = 0;
    float v = strtof(s, &end);
    if (errno != 0 || end == s || *end != '\0')
    {
        return -1;
    }
    *out = v;
    return 0;
}

int main(int argc, char *argv[])
{
    int width = 0;
    int height = 0;
    float quality = 0.0;
    int iterations = 0;
    int benchmark = 0;
    char *rgbi24_input_path = NULL;
    char *webp_output_path = NULL;

    /* === ARGUMENT PARSING === */

    /* Long-only; provide no short options (optstring = "") */
    static const struct option long_opts[] = {
        {"width", required_argument, NULL, 1},
        {"height", required_argument, NULL, 2},
        {"quality", required_argument, NULL, 3},
        {"iterations", required_argument, NULL, 4},
        {"benchmark", no_argument, NULL, 5},
        {"input", required_argument, NULL, 6},
        {"output", required_argument, NULL, 7},
        {"help", no_argument, NULL, 8},
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
        { /* --quality */
            if (parse_float(optarg, &quality) != 0)
            {
                fprintf(stderr, "Invalid --quality value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 4:
        { /* --iterations */
            if (parse_int(optarg, &iterations) != 0)
            {
                fprintf(stderr, "Invalid --iterations value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 5: /* --benchmark */
            benchmark = 1;
            break;
        case 6: /* --input */
            rgbi24_input_path = optarg;
            break;
        case 7: /* --output */
            webp_output_path = optarg;
            break;
        case 8: /* --help */
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
        unsigned char *rgbi24_input = inbuf;
        size_t rgbi24_input_size = inbuf_size;
        
        clock_t total_processing_time = 0;
        for(int i = 0; i < iterations; i++){
            clock_t t0 = clock();
            unsigned char *webp_output = NULL;
            size_t webp_output_size = WebPEncodeRGB(rgbi24_input, width, height, width * 3, quality, &webp_output);
            clock_t t1 = clock();
            WebPFree(webp_output);
            total_processing_time += t1 - t0;
        }
        fprintf(stderr, "Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
    }

    /* === ENCODER SETUP === */
    unsigned char *rgbi24_input = inbuf;
    size_t rgbi24_input_size = inbuf_size;
    
    size_t webp_output_size = 0;
    unsigned char *webp_output = NULL;

    /* === ENCODER TEST === */
    webp_output_size = WebPEncodeRGB(rgbi24_input, width, height, width * 3, quality, &webp_output);
    
    /* === ENCODER CLEANUP === */
    img_destroy(rgbi24_input);

    /* === ENCODED IMAGE OUTPUT === */
    if (!strcmp(webp_output_path, "-"))
    {
        int err = write_img_to_stdout(webp_output, webp_output_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = write_img_to_path(webp_output_path, webp_output, webp_output_size);
        if (err)
        {
            return err;
        }
    }
    img_destroy(webp_output);

    return EXIT_SUCCESS;
}