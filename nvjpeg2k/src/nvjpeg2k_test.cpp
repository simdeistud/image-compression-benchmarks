#define nvjpeg2kProgOrder   nvjpeg2kProgOrder_t
#define nvjpeg2kQualityType nvjpeg2kQualityType_t

#include <cuda_runtime_api.h>
#include <nvjpeg2k.h>

#undef nvjpeg2kProgOrder
#undef nvjpeg2kQualityType

#include <array>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define WARMUP_ITERS 10

#define CHECK_CUDA(call)                                                         \
    do {                                                                         \
        cudaError_t _e = (call);                                                 \
        if (_e != cudaSuccess) {                                                 \
            std::fprintf(stderr, "CUDA error %d (%s) at %s:%d\n",                \
                         static_cast<int>(_e), cudaGetErrorString(_e),           \
                         __FILE__, __LINE__);                                    \
            std::exit(EXIT_FAILURE);                                             \
        }                                                                        \
    } while (0)

#define CHECK_NVJ2K(call)                                                        \
    do {                                                                         \
        nvjpeg2kStatus_t _e = (call);                                            \
        if (_e != NVJPEG2K_STATUS_SUCCESS) {                                     \
            std::fprintf(stderr, "nvJPEG2K error %d at %s:%d\n",                 \
                         static_cast<int>(_e), __FILE__, __LINE__);              \
            std::exit(EXIT_FAILURE);                                             \
        }                                                                        \
    } while (0)

struct Options {
    int width = 0;
    int height = 0;
    int quality = 0;
    int iterations = 0;
    bool benchmark = false;
    std::string output_path;
};

static void print_syntax()
{
    std::printf("\nProgram Input Syntax:\n\n");
    std::printf("  ┌──────────────────────────────────────┐\n");
    std::printf("  │ -w  <width [px]>                     │\n");
    std::printf("  │ -h  <height [px]>                    │\n");
    std::printf("  │ -q  <quality [10...100]>             │\n");
    std::printf("  │ -i  <iterations [1...n]>             │\n");
    std::printf("  │ -b  benchmark mode                   │\n");
    std::printf("  │ -o  <output mode [FILEPATH|-]>       │\n");
    std::printf("  └──────────────────────────────────────┘\n");
}

static double now_s()
{
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    const auto dt = clock::now() - t0;
    return std::chrono::duration<double>(dt).count();
}

static int parse_int(const char *s)
{
    if (!s) return -1;
    char *end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (*s == '\0' || !end || *end != '\0') return -1;
    return static_cast<int>(v);
}

static Options parse_args(int argc, char **argv)
{
    Options opt{};

    for (int a = 1; a < argc; ++a) {
        if (!std::strcmp(argv[a], "-w") && a + 1 < argc) {
            opt.width = parse_int(argv[++a]);
        } else if (!std::strcmp(argv[a], "-h") && a + 1 < argc) {
            opt.height = parse_int(argv[++a]);
        } else if (!std::strcmp(argv[a], "-q") && a + 1 < argc) {
            opt.quality = parse_int(argv[++a]);
        } else if (!std::strcmp(argv[a], "-i") && a + 1 < argc) {
            opt.iterations = parse_int(argv[++a]);
        } else if (!std::strcmp(argv[a], "-b")) {
            opt.benchmark = true;
        } else if (!std::strcmp(argv[a], "-o") && a + 1 < argc) {
            opt.output_path = argv[++a];
        } else {
            print_syntax();
            std::exit(EXIT_FAILURE);
        }
    }

    if (opt.width <= 0 || opt.height <= 0 ||
        opt.quality < 10 || opt.quality > 100 ||
        opt.iterations <= 0 ||
        opt.output_path.empty()) {
        print_syntax();
        std::exit(EXIT_FAILURE);
    }

    return opt;
}

static std::vector<unsigned char> read_exact_stdin(std::size_t nbytes)
{
    std::vector<unsigned char> buf(nbytes);
    std::size_t off = 0;

    while (off < nbytes) {
        const std::size_t rd = std::fread(buf.data() + off, 1, nbytes - off, stdin);
        if (rd == 0) {
            if (std::ferror(stdin)) {
                std::fprintf(stderr, "stdin read error\n");
            } else {
                std::fprintf(stderr,
                             "stdin underflow: expected %zu bytes, got %zu bytes\n",
                             nbytes, off);
            }
            std::exit(EXIT_FAILURE);
        }
        off += rd;
    }

    return buf;
}

static void write_output(const std::string &path,
                         const unsigned char *buf,
                         std::size_t nbytes)
{
    FILE *f = nullptr;
    if (path == "-") {
        f = stdout;
    } else {
        f = std::fopen(path.c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "fopen(%s) failed: %s\n",
                         path.c_str(), std::strerror(errno));
            std::exit(EXIT_FAILURE);
        }
    }

    if (std::fwrite(buf, 1, nbytes, f) != nbytes) {
        std::fprintf(stderr, "write failed\n");
        if (f != stdout) std::fclose(f);
        std::exit(EXIT_FAILURE);
    }

    if (f == stdout) std::fflush(stdout);
    else std::fclose(f);
}

int main(int argc, char **argv)
{
    const Options opt = parse_args(argc, argv);
    const std::size_t rgb_size =
        static_cast<std::size_t>(opt.width) *
        static_cast<std::size_t>(opt.height) * 3u;

    std::vector<unsigned char> host_rgb = read_exact_stdin(rgb_size);

    cudaStream_t stream = nullptr;
    CHECK_CUDA(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

    nvjpeg2kEncoder_t enc_handle = nullptr;
    nvjpeg2kEncodeState_t enc_state = nullptr;
    nvjpeg2kEncodeParams_t enc_params = nullptr;

    CHECK_NVJ2K(nvjpeg2kEncoderCreateSimple(&enc_handle));
    CHECK_NVJ2K(nvjpeg2kEncodeStateCreate(enc_handle, &enc_state));
    CHECK_NVJ2K(nvjpeg2kEncodeParamsCreate(&enc_params));
    CHECK_NVJ2K(nvjpeg2kEncodeParamsSetInputFormat(enc_params, NVJPEG2K_FORMAT_INTERLEAVED));

    std::array<nvjpeg2kImageComponentInfo_t, 3> comp_info{};
    for (auto &c : comp_info) {
        c.component_width  = static_cast<uint32_t>(opt.width);
        c.component_height = static_cast<uint32_t>(opt.height);
        c.precision        = 8;
        c.sgn              = 0;
    }

    nvjpeg2kEncodeConfig_t enc_config{};
    enc_config.stream_type     = NVJPEG2K_STREAM_JP2;
    enc_config.color_space     = NVJPEG2K_COLORSPACE_SRGB;
    enc_config.image_width     = opt.width;
    enc_config.image_height    = opt.height;
    enc_config.num_components  = 3;
    enc_config.image_comp_info = comp_info.data();
    enc_config.code_block_w    = 64;
    enc_config.code_block_h    = 64;
    enc_config.irreversible    = 1;
    enc_config.mct_mode        = 1;
    enc_config.prog_order      = NVJPEG2K_LRCP;
    enc_config.num_resolutions = 6;
    enc_config.rsiz            = NVJPEG2K_RSIZ_HT;
    enc_config.encode_modes    = NVJPEG2K_MODE_HT;

    CHECK_NVJ2K(nvjpeg2kEncodeParamsSetEncodeConfig(enc_params, &enc_config));
    CHECK_NVJ2K(nvjpeg2kEncodeParamsSpecifyQuality(
        enc_params, NVJPEG2K_QUALITY_TYPE_Q_FACTOR, static_cast<double>(opt.quality)));

    unsigned char *d_rgb_in = nullptr;
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&d_rgb_in), rgb_size));

    std::array<void *, 3> enc_pixel_data{d_rgb_in, nullptr, nullptr};
    std::array<size_t, 3> enc_pitch_in_bytes{
        static_cast<size_t>(opt.width) * 3u, 0u, 0u
    };

    nvjpeg2kImage_t input_image{};
    input_image.pixel_data     = enc_pixel_data.data();
    input_image.pitch_in_bytes = enc_pitch_in_bytes.data();
    input_image.pixel_type     = NVJPEG2K_UINT8;
    input_image.num_components = 3;

    nvjpeg2kHandle_t dec_handle = nullptr;
    nvjpeg2kDecodeState_t dec_state = nullptr;
    nvjpeg2kStream_t jpeg2k_stream = nullptr;
    nvjpeg2kDecodeParams_t decode_params = nullptr;

    CHECK_NVJ2K(nvjpeg2kCreateSimple(&dec_handle));
    CHECK_NVJ2K(nvjpeg2kDecodeStateCreate(dec_handle, &dec_state));
    CHECK_NVJ2K(nvjpeg2kStreamCreate(&jpeg2k_stream));
    CHECK_NVJ2K(nvjpeg2kDecodeParamsCreate(&decode_params));
    CHECK_NVJ2K(nvjpeg2kDecodeParamsSetRGBOutput(decode_params, 1));
    CHECK_NVJ2K(nvjpeg2kDecodeParamsSetOutputFormat(
        decode_params, NVJPEG2K_FORMAT_INTERLEAVED));

    unsigned char *d_rgb_out = nullptr;
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&d_rgb_out), rgb_size));

    std::vector<unsigned char> host_rgb_out(rgb_size);

    std::array<void *, 3> dec_pixel_data{d_rgb_out, nullptr, nullptr};
    std::array<size_t, 3> dec_pitch_in_bytes{
        static_cast<size_t>(opt.width) * 3u, 0u, 0u
    };

    nvjpeg2kImage_t output_image{};
    output_image.pixel_data     = dec_pixel_data.data();
    output_image.pitch_in_bytes = dec_pitch_in_bytes.data();
    output_image.pixel_type     = NVJPEG2K_UINT8;
    output_image.num_components = 3;

    double total_s = 0.0;

    const int total_iters = WARMUP_ITERS + opt.iterations;
    for (int it = 0; it < total_iters; ++it) {
        const bool timed = (it >= WARMUP_ITERS);
        const double t0 = timed ? now_s() : 0.0;

        CHECK_CUDA(cudaMemcpyAsync(
            d_rgb_in, host_rgb.data(), rgb_size, cudaMemcpyHostToDevice, stream));

        CHECK_NVJ2K(nvjpeg2kEncode(
            enc_handle, enc_state, enc_params, &input_image, stream));

        size_t bs_size = 0;
        CHECK_NVJ2K(nvjpeg2kEncodeRetrieveBitstream(
            enc_handle, enc_state, nullptr, &bs_size, stream));

        std::vector<unsigned char> bitstream(bs_size);
        size_t actual_size = bs_size;
        CHECK_NVJ2K(nvjpeg2kEncodeRetrieveBitstream(
            enc_handle, enc_state, bitstream.data(), &actual_size, stream));

        CHECK_CUDA(cudaStreamSynchronize(stream));
        bitstream.resize(actual_size);

        CHECK_NVJ2K(nvjpeg2kStreamParse(
            dec_handle, bitstream.data(), bitstream.size(), 0, 0, jpeg2k_stream));

        nvjpeg2kImageInfo_t image_info{};
        CHECK_NVJ2K(nvjpeg2kStreamGetImageInfo(jpeg2k_stream, &image_info));

        if (static_cast<int>(image_info.image_width) != opt.width ||
            static_cast<int>(image_info.image_height) != opt.height) {
            std::fprintf(stderr, "round-trip dimension mismatch\n");
            std::exit(EXIT_FAILURE);
        }

        CHECK_NVJ2K(nvjpeg2kDecodeImage(
            dec_handle, dec_state, jpeg2k_stream, decode_params, &output_image, stream));

        CHECK_CUDA(cudaMemcpyAsync(
            host_rgb_out.data(), d_rgb_out, rgb_size, cudaMemcpyDeviceToHost, stream));

        CHECK_CUDA(cudaStreamSynchronize(stream));

        if (timed) total_s += (now_s() - t0);
    }

    if (opt.benchmark) {
        if (opt.output_path != "-") {
            write_output(opt.output_path, host_rgb_out.data(), host_rgb_out.size());
        }
        std::printf("%.9f\n", total_s);
    } else {
        write_output(opt.output_path, host_rgb_out.data(), host_rgb_out.size());
    }

    CHECK_CUDA(cudaFree(d_rgb_in));
    CHECK_CUDA(cudaFree(d_rgb_out));

    CHECK_NVJ2K(nvjpeg2kEncodeParamsDestroy(enc_params));
    CHECK_NVJ2K(nvjpeg2kEncodeStateDestroy(enc_state));
    CHECK_NVJ2K(nvjpeg2kEncoderDestroy(enc_handle));

    CHECK_NVJ2K(nvjpeg2kDecodeParamsDestroy(decode_params));
    CHECK_NVJ2K(nvjpeg2kStreamDestroy(jpeg2k_stream));
    CHECK_NVJ2K(nvjpeg2kDecodeStateDestroy(dec_state));
    CHECK_NVJ2K(nvjpeg2kDestroy(dec_handle));

    CHECK_CUDA(cudaStreamDestroy(stream));

    return EXIT_SUCCESS;
}