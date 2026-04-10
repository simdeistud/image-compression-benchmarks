#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#define IMG_IO_IMPLEMENTATION
#include "img_io.h"
#include <libgpujpeg/gpujpeg.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Options:\n"
            "  --width <int>                Input image width (px)\n"
            "  --height <int>               Input image height (px)\n"
            "  --subsampling <int>          Selected subsampling [444|422|420]\n"
            "  --quality <int>              Selected quality [0...100]\n"
            "  --pixel_format <str>         Selected pixel format [planar|interleaved]\n"
            "  --restart_interval <int>     Selected restart interval [0...N]\n"
            "  --iterations <int>           Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                  Benchmark mode (flag)\n"
            "  --input <path>|-             Selected jpeg input [PATH|stdin]\n"
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

int subsampling_to_gpujpegsamp(const int subsampling)
{
    switch (subsampling)
    {
    case 444: return GPUJPEG_SUBSAMPLING_444;
    case 422: return GPUJPEG_SUBSAMPLING_422;
    case 420: return GPUJPEG_SUBSAMPLING_420;
    default: return GPUJPEG_SUBSAMPLING_UNKNOWN;
    }
}

int is_interleaved(const char *pixel_format_str)
{
    if (strcmp(pixel_format_str, "planar") == 0)
        return 0;
    if (strcmp(pixel_format_str, "interleaved") == 0)
        return 1;
    return -1;
}

int main(int argc, char *argv[])
{
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int quality = 0;
    char* pixel_format = NULL;
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
        {"pixel_format", required_argument, NULL, 5},
        {"restart_interval", required_argument, NULL, 6},
        {"iterations", required_argument, NULL, 7},
        {"benchmark", no_argument, NULL, 8},
        {"input", required_argument, NULL, 9},
        {"output", required_argument, NULL, 10},
        {"help", no_argument, NULL, 11},
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
        case 5: /* --pixel_format */
            pixel_format = optarg;
            break;
        case 6:
        { /* --restart_interval */
            if (parse_int(optarg, &restart_interval) != 0)
            {
                fprintf(stderr, "Invalid --restart_interval value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 7:
        { /* --iterations */
            if (parse_int(optarg, &iterations) != 0)
            {
                fprintf(stderr, "Invalid --iterations value: '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        break;
        case 8: /* --benchmark */
            benchmark = 1;
            break;
        case 9: /* --input */
            rgbi24_input_path = optarg;
            break;
        case 10: /* --output */
            jpeg_output_path = optarg;
            break;
        case 11: /* --help */
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
        struct gpujpeg_parameters param;
        struct gpujpeg_image_parameters param_image;
        struct gpujpeg_encoder* encoder;
        struct gpujpeg_encoder_input encoder_input;

        encoder = gpujpeg_encoder_create(0);
        if (encoder == NULL)
        {
            perror("Failed to create encoder");
            return 1;
        }

        gpujpeg_image_set_default_parameters(&param_image);
        param_image.width = width;
        param_image.height = height;
        param_image.color_space = GPUJPEG_RGB;
        param_image.pixel_format = GPUJPEG_444_U8_P012;
        gpujpeg_set_default_parameters(&param);
        param.quality = quality;
        param.interleaved = is_interleaved(pixel_format);
        param.segment_info = param.interleaved;
        param.restart_interval = restart_interval;
        gpujpeg_parameters_chroma_subsampling(&param, subsampling_to_gpujpegsamp(subsampling));

        unsigned char *rgbi24_input = inbuf;
        size_t rgbi24_input_size = inbuf_size;

        clock_t total_processing_time = 0;
        for(int i = 0; i < iterations; i++){
            clock_t t0 = clock();
            gpujpeg_encoder_input_set_image(&encoder_input, rgbi24_input);
            size_t jpeg_output_size = 0;
            unsigned char *jpeg_output = NULL;
            gpujpeg_encoder_encode(encoder, &param, &param_image, &encoder_input, &jpeg_output, &jpeg_output_size);
            clock_t t1 = clock();
            total_processing_time += t1 - t0;
            //usleep(17 * 1000);
        }
        fprintf(stderr, "Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        gpujpeg_encoder_destroy(encoder);
    }

    /* === ENCODER SETUP === */
    struct gpujpeg_parameters param;
    struct gpujpeg_image_parameters param_image;
    struct gpujpeg_encoder* encoder;
    struct gpujpeg_encoder_input encoder_input;

    encoder = gpujpeg_encoder_create(0);
    if (encoder == NULL)
    {
        perror("Failed to create encoder");
        return 1;
    }

    gpujpeg_image_set_default_parameters(&param_image);
    param_image.width = width;
    param_image.height = height;
    param_image.color_space = GPUJPEG_RGB;
    param_image.pixel_format = GPUJPEG_444_U8_P012;
    gpujpeg_set_default_parameters(&param);
    param.quality = quality;
    param.interleaved = is_interleaved(pixel_format);
    param.segment_info = param.interleaved;
    param.restart_interval = restart_interval;
    gpujpeg_parameters_chroma_subsampling(&param, subsampling_to_gpujpegsamp(subsampling));

    unsigned char *rgbi24_input = inbuf;
    size_t rgbi24_input_size = inbuf_size;

    /* === ENCODER TEST === */
    gpujpeg_encoder_input_set_image(&encoder_input, rgbi24_input);
    size_t jpeg_output_size = 0;
    unsigned char *jpeg_output = NULL;
    if (gpujpeg_encoder_encode(encoder, &param, &param_image, &encoder_input, &jpeg_output, &jpeg_output_size))
    {
        perror("Failed to encode image");
        return 1;
    }
    
    /* === ENCODER CLEANUP === */
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
    gpujpeg_encoder_destroy(encoder);

    return EXIT_SUCCESS;
}