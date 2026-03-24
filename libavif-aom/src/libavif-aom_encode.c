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
            "Options:\n"
            "  --width <int>                Input image width (px)\n"
            "  --height <int>               Input image height (px)\n"
            "  --subsampling <string>       Chroma subsampling [444|422|420]\n"
            "  --quality <int>              Selected quality [%d...%d]\n"
            "  --iterations <int>           Selected iterations [>0 if --benchmark]\n"
            "  --benchmark                  Benchmark mode (flag)\n"
            "  --input <path>|-             Selected avif input [PATH|stdin]\n"
            "  --output <path>|-            Selected RGBI24 output [PATH|stdout]\n"
            "  --help                       Show this help and exit\n",
            prog, AVIF_QUALITY_WORST, AVIF_QUALITY_BEST);
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
    avifPixelFormat subsampling = 0;
    int quality = 0;
    int iterations = 0;
    int benchmark = 0;
    char *rgbi24_input_path = NULL;
    char *avif_output_path = NULL;

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
            if (strcmp(optarg, "444") == 0) {
                subsampling = AVIF_PIXEL_FORMAT_YUV444;
            } else if (strcmp(optarg, "422") == 0) {
                subsampling = AVIF_PIXEL_FORMAT_YUV422;
            } else if (strcmp(optarg, "420") == 0) {
                subsampling = AVIF_PIXEL_FORMAT_YUV420;
            } else {
                fprintf(stderr, "Invalid --subsampling value: '%s' (expected '444', '422', or '420')\n", optarg);
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
            avif_output_path = optarg;
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

    if (benchmark){
        /* === ENCODER BENCHMARK === */
        avifRGBImage rgbi24_input;
        size_t rgbi24_input_size = width * height * 3;
        rgbi24_input.width  = width;
        rgbi24_input.height = height;
        rgbi24_input.depth  = 8;
        rgbi24_input.format = AVIF_RGB_FORMAT_RGB;
        rgbi24_input.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_FASTEST;
        rgbi24_input.chromaDownsampling = AVIF_CHROMA_DOWNSAMPLING_FASTEST;
        rgbi24_input.avoidLibYUV = AVIF_FALSE;
        rgbi24_input.ignoreAlpha = AVIF_FALSE;
        rgbi24_input.alphaPremultiplied = AVIF_FALSE;
        rgbi24_input.isFloat = AVIF_FALSE;
        rgbi24_input.maxThreads = 1;
        if (!strcmp(rgbi24_input_path, "-"))
        {
            int err = load_img_from_stdin(&rgbi24_input.pixels, &rgbi24_input_size);
            if (err)
            {
                return err;
            }
        }
        else
        {
            int err = load_img_from_path(rgbi24_input_path, &rgbi24_input.pixels, &rgbi24_input_size);
            if (err)
            {
                return err;
            }
        }
        rgbi24_input.rowBytes = width * 3;
        clock_t total_processing_time = 0;
        for(int i = 0; i < iterations; i++){
            clock_t t0 = clock();
            avifEncoder *encoder = avifEncoderCreate();
            if (!encoder) {
                fprintf(stderr, "Out of memory\n");
                return EXIT_FAILURE;
            }
            encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
            encoder->maxThreads = 1;
            encoder->speed =  AVIF_SPEED_FASTEST;
            encoder->quality = quality;
            encoder->qualityAlpha = 0;
            avifImage* avif_image = avifImageCreate(width, height, 8, subsampling);
            avifRWData avif_output = AVIF_DATA_EMPTY;
            if (!avif_image) {
                fprintf(stderr, "Out of memory\n");
                return EXIT_FAILURE;
            }
            avifResult convertResult = avifImageRGBToYUV(avif_image, &rgbi24_input);
            if (convertResult != AVIF_RESULT_OK) {
                fprintf(stderr, "Failed to convert to YUV(A): %s\n", avifResultToString(convertResult));
                return EXIT_FAILURE;
            }
            avifResult addImageResult = avifEncoderAddImage(encoder, avif_image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
            if (addImageResult != AVIF_RESULT_OK) {
                fprintf(stderr, "Failed to add image to encoder: %s\n", avifResultToString(addImageResult));
                if (encoder->diag.error[0] != '\0') {
                    fprintf(stderr, "  %s\n", encoder->diag.error);
                }
                return EXIT_FAILURE;
            }
            avifResult finishResult = avifEncoderFinish(encoder, &avif_output);
            if (finishResult != AVIF_RESULT_OK) {
                fprintf(stderr, "Failed to finish encode: %s\n", avifResultToString(finishResult));
                return EXIT_FAILURE;
            }
            avifImageDestroy(avif_image);
            avifRWDataFree(&avif_output);
            avifEncoderDestroy(encoder);
            clock_t t1 = clock();
            total_processing_time += t1 - t0;
        }
        printf("Total processing time (seconds):%f\n", ((double)total_processing_time) / CLOCKS_PER_SEC);
        printf("Average processing time per iteration (milliseconds):%f\n", ((double)total_processing_time) / iterations / CLOCKS_PER_SEC * 1000);
        printf("Average frames per second:%f\n", iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        printf("Average megapixels per second:%f\n", (width * height) / (double)1000000 * iterations / (((double)total_processing_time) / CLOCKS_PER_SEC));
        img_destroy(rgbi24_input.pixels);
    }

    /* === ENCODER CREATION === */
    avifEncoder *encoder = avifEncoderCreate();
    if (!encoder) {
        fprintf(stderr, "Out of memory\n");
        return EXIT_FAILURE;
    }
    encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
    encoder->maxThreads = 1;
    encoder->speed =  AVIF_SPEED_FASTEST;
    encoder->quality = quality;
    encoder->qualityAlpha = 0;

    /* === ENCODER SETUP === */
    avifRGBImage rgbi24_input;
    size_t rgbi24_input_size = width * height * 3;
    rgbi24_input.width  = width;
    rgbi24_input.height = height;
    rgbi24_input.depth  = 8;
    rgbi24_input.format = AVIF_RGB_FORMAT_RGB;
    rgbi24_input.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_FASTEST;
    rgbi24_input.chromaDownsampling = AVIF_CHROMA_DOWNSAMPLING_FASTEST;
    rgbi24_input.avoidLibYUV = AVIF_FALSE;
    rgbi24_input.ignoreAlpha = AVIF_FALSE;
    rgbi24_input.alphaPremultiplied = AVIF_FALSE;
    rgbi24_input.isFloat = AVIF_FALSE;
    rgbi24_input.maxThreads = 1;
    if (!strcmp(rgbi24_input_path, "-"))
    {
        int err = load_img_from_stdin(&rgbi24_input.pixels, &rgbi24_input_size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = load_img_from_path(rgbi24_input_path, &rgbi24_input.pixels, &rgbi24_input_size);
        if (err)
        {
            return err;
        }
    }
    rgbi24_input.rowBytes = width * 3;
    avifImage* avif_image = avifImageCreate(width, height, 8, subsampling);
    avifRWData avif_output = AVIF_DATA_EMPTY;
    if (!avif_image) {
        fprintf(stderr, "Out of memory\n");
        return EXIT_FAILURE;
    }
    avifResult convertResult = avifImageRGBToYUV(avif_image, &rgbi24_input);
    if (convertResult != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to convert to YUV(A): %s\n", avifResultToString(convertResult));
        return EXIT_FAILURE;
    }
    
    /* === ENCODER TEST === */
    avifResult addImageResult = avifEncoderAddImage(encoder, avif_image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
    if (addImageResult != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to add image to encoder: %s\n", avifResultToString(addImageResult));
        if (encoder->diag.error[0] != '\0') {
            fprintf(stderr, "  %s\n", encoder->diag.error);
        }
        return EXIT_FAILURE;
    }
    avifResult finishResult = avifEncoderFinish(encoder, &avif_output);
    if (finishResult != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to finish encode: %s\n", avifResultToString(finishResult));
        return EXIT_FAILURE;
    }

    /* === ENCODER CLEANUP === */
    img_destroy(rgbi24_input.pixels);

    /* === ENCODED IMAGE OUTPUT === */
    if (!strcmp(avif_output_path, "-"))
    {
        int err = write_img_to_stdout(avif_output.data, avif_output.size);
        if (err)
        {
            return err;
        }
    }
    else
    {
        int err = write_img_to_path(avif_output_path, avif_output.data, avif_output.size);
        if (err)
        {
            return err;
        }
    }
    avifImageDestroy(avif_image);
    avifEncoderDestroy(encoder);
    avifRWDataFree(&avif_output);

    return EXIT_SUCCESS;
}