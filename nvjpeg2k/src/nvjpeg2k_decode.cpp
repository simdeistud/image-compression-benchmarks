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
    int iterations = 0;
    bool benchmark = false;
    std::string output_path;
};

static void print_syntax()
{
    std::printf("\nProgram Input Syntax:\n\n");
    std::printf("  ┌──────────────────────────────────────┐\n");
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
        if (!std::strcmp(argv[a], "-i") && a + 1 < argc) {
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

    if (opt.iterations <= 0 || opt.output_path.empty()) {
        print_syntax();
        std::exit(EXIT_FAILURE);
    }

    return opt;
}

static std::vector<unsigned char> read_all_stdin()
{
    std::vector<unsigned char> data;
    constexpr std::size_t chunk_size = 1u << 20;
    std::array<unsigned char, chunk_size> chunk{};

    while (true) {
        const std::size_t rd = std::fread(chunk.data(), 1, chunk.size(), stdin);
        if (rd > 0) data.insert(data.end(), chunk.data(), chunk.data() + rd);

        if (rd < chunk.size()) {
            if (std::ferror(stdin)) {
                std::fprintf(stderr, "stdin read error\n");
                std::exit(EXIT_FAILURE);
            }
            break;
        }
    }

    return data;
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
    std::vector<unsigned char> bitstream = read_all_stdin();

    if (bitstream.empty()) {
        std::fprintf(stderr, "stdin is empty\n");
        return EXIT_FAILURE;
    }

    cudaStream_t stream = nullptr;
    CHECK_CUDA(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

    nvjpeg2kHandle_t handle = nullptr;
    nvjpeg2kDecodeState_t decode_state = nullptr;
    nvjpeg2kStream_t jpeg2k_stream = nullptr;
    nvjpeg2kDecodeParams_t decode_params = nullptr;

    CHECK_NVJ2K(nvjpeg2kCreateSimple(&handle));
    CHECK_NVJ2K(nvjpeg2kDecodeStateCreate(handle, &decode_state));
    CHECK_NVJ2K(nvjpeg2kStreamCreate(&jpeg2k_stream));
    CHECK_NVJ2K(nvjpeg2kDecodeParamsCreate(&decode_params));

    CHECK_NVJ2K(nvjpeg2kStreamParse(
        handle, bitstream.data(), bitstream.size(), 0, 0, jpeg2k_stream));

    nvjpeg2kImageInfo_t image_info{};
    CHECK_NVJ2K(nvjpeg2kStreamGetImageInfo(jpeg2k_stream, &image_info));

    CHECK_NVJ2K(nvjpeg2kDecodeParamsSetRGBOutput(decode_params, 1));
    CHECK_NVJ2K(nvjpeg2kDecodeParamsSetOutputFormat(
        decode_params, NVJPEG2K_FORMAT_INTERLEAVED));

    const std::size_t host_rgb_size =
        static_cast<std::size_t>(image_info.image_width) *
        static_cast<std::size_t>(image_info.image_height) * 3u;

    std::vector<unsigned char> host_rgb(host_rgb_size);

    unsigned char *d_rgb = nullptr;
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&d_rgb), host_rgb_size));

    std::array<void *, 3> pixel_data{d_rgb, nullptr, nullptr};
    std::array<size_t, 3> pitch_in_bytes{
        static_cast<size_t>(image_info.image_width) * 3u, 0u, 0u
    };

    nvjpeg2kImage_t output_image{};
    output_image.pixel_data     = pixel_data.data();
    output_image.pitch_in_bytes = pitch_in_bytes.data();
    output_image.pixel_type     = NVJPEG2K_UINT8;
    output_image.num_components = 3;

    double total_s = 0.0;
    const int total_iters = WARMUP_ITERS + opt.iterations;

    for (int it = 0; it < total_iters; ++it) {
        const bool timed = (it >= WARMUP_ITERS);
        const double t0 = timed ? now_s() : 0.0;

        CHECK_NVJ2K(nvjpeg2kDecodeImage(
            handle, decode_state, jpeg2k_stream, decode_params, &output_image, stream));

        CHECK_CUDA(cudaMemcpyAsync(
            host_rgb.data(), d_rgb, host_rgb_size, cudaMemcpyDeviceToHost, stream));

        CHECK_CUDA(cudaStreamSynchronize(stream));

        if (timed) total_s += (now_s() - t0);
    }

    if (opt.benchmark) {
        if (opt.output_path != "-") {
            write_output(opt.output_path, host_rgb.data(), host_rgb.size());
        }
        std::printf("%.9f\n", total_s);
    } else {
        write_output(opt.output_path, host_rgb.data(), host_rgb.size());
    }

    CHECK_CUDA(cudaFree(d_rgb));
    CHECK_NVJ2K(nvjpeg2kDecodeParamsDestroy(decode_params));
    CHECK_NVJ2K(nvjpeg2kStreamDestroy(jpeg2k_stream));
    CHECK_NVJ2K(nvjpeg2kDecodeStateDestroy(decode_state));
    CHECK_NVJ2K(nvjpeg2kDestroy(handle));
    CHECK_CUDA(cudaStreamDestroy(stream));

    return EXIT_SUCCESS;
}