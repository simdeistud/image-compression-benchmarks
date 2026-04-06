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
            "Basic options:\n"
            "  --iterations <int>         Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                Benchmark mode (flag)\n"
            "  --input <path>|-           Selected jpeg [PATH|stdin]\n"
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
    char *jpeg_input_path = NULL;
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
            jpeg_input_path = optarg;
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
        struct gpujpeg_decoder_output decoder_output;
        struct gpujpeg_parameters param;
        struct gpujpeg_image_parameters param_image;
        struct gpujpeg_decoder* decoder;

        unsigned char *jpeg_input = NULL;
        size_t jpeg_input_size = 0;
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

        if (gpujpeg_init_device(0, 0))
        {
            perror("Failed to initialize GPU device");
            return 1;
        }

        decoder = gpujpeg_decoder_create(0);
        if (decoder == NULL)
        {
            perror("Failed to create decoder");
            return 1;
        }
        gpujpeg_set_default_parameters(&param);
        gpujpeg_image_set_default_parameters(&param_image);
        gpujpeg_decoder_init(decoder, &param, &param_image);
        gpujpeg_decoder_output_set_default(&decoder_output);
        gpujpeg_decoder_set_output_format(decoder, GPUJPEG_RGB, GPUJPEG_444_U8_P012);
        clock_t total_processing_time = 0;
        int width = 0, height = 0;
        for(int i = 0; i < iterations; ++i)
        {
            clock_t t0 = clock();
            gpujpeg_decoder_decode(decoder, jpeg_input, jpeg_input_size, &decoder_output);
            clock_t t1 = clock();
            total_processing_time += t1 - t0;
            //usleep(17 * 1000);
        }
        printf("Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        printf("Average processing time per iteration (milliseconds):%f\n", ((double)total_processing_time) / iterations / CLOCKS_PER_SEC * 1000);
        printf("Average frames per second:%f\n", iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        printf("Average megapixels per second:%f\n", (decoder_output.data_size / 3) / (double)1000000 * iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        gpujpeg_decoder_destroy(decoder);
    }

    /* === DECODER SETUP === */
    struct gpujpeg_decoder_output decoder_output;
    struct gpujpeg_parameters param;
    struct gpujpeg_image_parameters param_image;
    struct gpujpeg_decoder* decoder;

    unsigned char *jpeg_input = NULL;
    size_t jpeg_input_size = 0;
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

    if (gpujpeg_init_device(0, 0))
    {
        perror("Failed to initialize GPU device");
        return 1;
    }

    decoder = gpujpeg_decoder_create(0);
    if (decoder == NULL)
    {
        perror("Failed to create decoder");
        return 1;
    }
    gpujpeg_set_default_parameters(&param);
    gpujpeg_image_set_default_parameters(&param_image);
    gpujpeg_decoder_init(decoder, &param, &param_image);
    gpujpeg_decoder_output_set_default(&decoder_output);
    gpujpeg_decoder_set_output_format(decoder, GPUJPEG_RGB, GPUJPEG_444_U8_P012);
    

    /* === DECODER TEST === */
    if (gpujpeg_decoder_decode(decoder, jpeg_input, jpeg_input_size, &decoder_output))
    {
        perror("Failed to decode image");
        return 1;
    }
    size_t rgbi24_output_size = decoder_output.data_size;
    unsigned char *rgbi24_output = malloc(rgbi24_output_size);
    memcpy(rgbi24_output, decoder_output.data, rgbi24_output_size);

    /* === DECODER CLEANUP === */
    gpujpeg_decoder_destroy(decoder);

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