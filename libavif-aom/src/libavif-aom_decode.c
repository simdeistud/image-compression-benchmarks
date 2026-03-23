#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#define IMG_IO_IMPLEMENTATION
#include "img_io.h"
#include <avif/avif.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Basic options:\n"
            "  --iterations <int>         Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                Benchmark mode (flag)\n"
            "  --input <path>|-           Selected avif [PATH|stdin]\n"
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
    char *avif_input_path = NULL;
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
            avif_input_path = optarg;
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
        avifDecoder* decoder = avifDecoderCreate();
        unsigned char *avif_input = NULL;
        size_t avif_input_size = 0;
        if (!strcmp(avif_input_path, "-"))
        {
            int err = load_img_from_stdin(&avif_input, &avif_input_size);
            if (err)
            {
                return err;
            }
        }
        else
        {
            int err = load_img_from_path(avif_input_path, &avif_input, &avif_input_size);
            if (err)
            {
                return err;
            }
        }
        clock_t total_processing_time = 0;
        int width = 0, height = 0;
        for(int i = 0; i < iterations; ++i)
        {
            clock_t t0 = clock();
            avifRGBImage rgbi24_output;
            avifDecoderSetIOMemory(decoder, avif_input, avif_input_size);
            avifDecoderParse(decoder);
            width = rgbi24_output.width;
            height = rgbi24_output.height;
            size_t rgbi24_output_size = width * height * 3;
            avifDecoderNextImage(decoder);
            avifRGBImageSetDefaults(&rgbi24_output, decoder->image);
            avifRGBImageAllocatePixels(&rgbi24_output);
            avifImageYUVToRGB(decoder->image, &rgbi24_output);
            clock_t t1 = clock();
            avifRGBImageFreePixels(&rgbi24_output);
            total_processing_time += t1 - t0;
        }
        printf("Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        printf("Average processing time per iteration (milliseconds):%f\n", ((double)total_processing_time) / iterations / CLOCKS_PER_SEC * 1000);
        printf("Average frames per second:%f\n", iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        printf("Average megapixels per second:%f\n", (width * height) / (double)1000000 * iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        avifDecoderDestroy(decoder);
        img_destroy(avif_input);
    }

    /* === DECODER CREATION === */
    avifDecoder* decoder = avifDecoderCreate();

    /* === DECODER SETUP === */
    unsigned char *avif_input = NULL;
    size_t avif_input_size = 0;
    if (!strcmp(avif_input_path, "-"))
    {
        int err = load_img_from_stdin(&avif_input, &avif_input_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = load_img_from_path(avif_input_path, &avif_input, &avif_input_size);
        if (err)
        {
            return err;
        }
    }

    /* === DECODER TEST === */
    avifRGBImage rgbi24_output;
    if (avifDecoderSetIOMemory(decoder, avif_input, avif_input_size) != AVIF_RESULT_OK) {
        fprintf(stderr, "Cannot set IO on avifDecoder: %s\n", avif_input_path);
        return EXIT_FAILURE;
    }
    int result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to decode image: %s\n", avifResultToString(result));
        return EXIT_FAILURE;
    }
    size_t rgbi24_output_size = rgbi24_output.width * rgbi24_output.height * 3;
    result = avifDecoderNextImage(decoder);
    if(result != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to decode image: %s\n", avifResultToString(result));
        return EXIT_FAILURE;
    }
    avifRGBImageSetDefaults(&rgbi24_output, decoder->image);
    result =  avifRGBImageAllocatePixels(&rgbi24_output);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "Allocation of RGB samples failed: %s (%s)\n", avif_input_path, avifResultToString(result));
        return EXIT_FAILURE;
    }
    result = avifImageYUVToRGB(decoder->image, &rgbi24_output);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to convert YUV to RGB: %s (%s)\n", avif_input_path, avifResultToString(result));
        return EXIT_FAILURE;
    }

    /* === DECODER CLEANUP === */
    avifDecoderDestroy(decoder);
    img_destroy(avif_input);

    /* === DECODED IMAGE OUTPUT === */
    if (!strcmp(rgbi24_output_path, "-"))
    {
        int err = write_img_to_stdout(rgbi24_output.pixels, rgbi24_output_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = write_img_to_path(rgbi24_output_path, rgbi24_output.pixels, rgbi24_output_size);
        if (err)
        {
            return err;
        }
    }
    avifRGBImageFreePixels(&rgbi24_output);

    return EXIT_SUCCESS;
}