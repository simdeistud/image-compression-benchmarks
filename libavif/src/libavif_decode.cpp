#include <avif/avif.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Seconds = std::chrono::duration<double>;

struct Options {
    int iterations = 1;
    bool benchmark = false;
    std::optional<std::string> output;
    std::string backend = "aom";
    bool help = false;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void print_help(std::ostream& os, const char* argv0) {
    os << "libavif_decode - AVIF -> RGB24 decoder benchmark\n\n"
       << "Syntax:\n"
       << "  " << argv0 << " [--iterations N] --backend {aom|dav1d|libgav1} [--benchmark] [--output PATH|-]\n\n"
       << "Notes:\n"
       << "  * Input AVIF bitstream is read from stdin.\n"
       << "  * Output is raw interleaved RGB24.\n"
       << "  * If --benchmark is enabled, --output is forbidden.\n"
       << "  * If --output is supplied, benchmark/iterations are disabled and a single decode is performed.\n"
       << "  * 10 warmup iterations are always executed before measured iterations in benchmark mode.\n";
}

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        auto need_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) fail("Missing value for " + name);
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            opt.help = true;
        } else if (arg == "--iterations" || arg == "-i") {
            opt.iterations = std::stoi(need_value("--iterations"));
        } else if (arg == "--benchmark" || arg == "-b") {
            opt.benchmark = true;
        } else if (arg == "--output" || arg == "-o") {
            opt.output = need_value("--output");
        } else if (arg == "--backend") {
            opt.backend = need_value("--backend");
        } else {
            fail("Unknown argument: " + std::string(arg));
        }
    }

    if (opt.help) return opt;
    if (opt.iterations <= 0) fail("--iterations must be positive");
    if (opt.backend != "aom" && opt.backend != "dav1d" && opt.backend != "libgav1") {
        fail("--backend must be one of: aom, dav1d, libgav1");
    }
    if (opt.output.has_value() && opt.benchmark) {
        fail("--output cannot be used together with --benchmark");
    }
    if (opt.output.has_value() && opt.iterations != 1) {
        fail("--output cannot be used together with --iterations != 1");
    }
    return opt;
}

std::vector<uint8_t> read_all_stdin() {
    std::istreambuf_iterator<char> begin(std::cin), end;
    std::vector<char> bytes(begin, end);
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

void write_all(const std::vector<uint8_t>& data, const std::string& path) {
    if (path == "-") {
        std::cout.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return;
    }
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) fail("Cannot open output file: " + path);
    ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

avifCodecChoice codec_choice_from_backend(const std::string& backend) {
    if (backend == "aom") return AVIF_CODEC_CHOICE_AOM;
    if (backend == "dav1d") return AVIF_CODEC_CHOICE_DAV1D;
    if (backend == "libgav1") return AVIF_CODEC_CHOICE_LIBGAV1;
    fail("Unsupported backend");
}

std::vector<uint8_t> decode_once(const Options& opt, const std::vector<uint8_t>& avif_bytes) {
    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) fail("avifDecoderCreate failed");
    decoder->codecChoice = codec_choice_from_backend(opt.backend);
    decoder->maxThreads = 1;

    avifResult result = avifDecoderSetIOMemory(decoder, avif_bytes.data(), avif_bytes.size());
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        fail(std::string("avifDecoderSetIOMemory failed: ") + avifResultToString(result));
    }
    result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        fail(std::string("avifDecoderParse failed: ") + avifResultToString(result));
    }
    result = avifDecoderNextImage(decoder);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        fail(std::string("avifDecoderNextImage failed: ") + avifResultToString(result));
    }

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.format = AVIF_RGB_FORMAT_RGB;
    rgb.depth = 8;
    rgb.maxThreads = 1;
    result = avifRGBImageAllocatePixels(&rgb);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        fail(std::string("avifRGBImageAllocatePixels failed: ") + avifResultToString(result));
    }
    result = avifImageYUVToRGB(decoder->image, &rgb);
    if (result != AVIF_RESULT_OK) {
        avifRGBImageFreePixels(&rgb);
        avifDecoderDestroy(decoder);
        fail(std::string("avifImageYUVToRGB failed: ") + avifResultToString(result));
    }

    const std::size_t width = static_cast<std::size_t>(decoder->image->width);
    const std::size_t height = static_cast<std::size_t>(decoder->image->height);
    std::vector<uint8_t> out(width * height * 3u);
    for (uint32_t y = 0; y < static_cast<uint32_t>(height); ++y) {
        std::memcpy(out.data() + static_cast<std::size_t>(y) * width * 3u,
                    rgb.pixels + static_cast<std::size_t>(y) * rgb.rowBytes,
                    width * 3u);
    }

    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);
    return out;
}

void benchmark(const Options& opt, const std::vector<uint8_t>& avif_bytes) {
    auto make_decoder = [&]() -> avifDecoder* {
        avifDecoder* d = avifDecoderCreate();
        if (!d) fail("avifDecoderCreate failed");
        d->codecChoice = codec_choice_from_backend(opt.backend);
        d->maxThreads = 1;
        return d;
    };

    auto one_cycle = [&](bool measure, double& setup_acc, double& process_acc, double& reset_acc) {
        avifDecoder* decoder = make_decoder();
        avifRGBImage rgb;
        bool rgb_allocated = false;

        auto t0 = Clock::now();
        avifResult result = avifDecoderSetIOMemory(decoder, avif_bytes.data(), avif_bytes.size());
        if (result != AVIF_RESULT_OK) fail(std::string("avifDecoderSetIOMemory failed: ") + avifResultToString(result));
        result = avifDecoderParse(decoder);
        if (result != AVIF_RESULT_OK) fail(std::string("avifDecoderParse failed: ") + avifResultToString(result));
        auto t1 = Clock::now();

        result = avifDecoderNextImage(decoder);
        if (result != AVIF_RESULT_OK) fail(std::string("avifDecoderNextImage failed: ") + avifResultToString(result));
        avifRGBImageSetDefaults(&rgb, decoder->image);
        rgb.format = AVIF_RGB_FORMAT_RGB;
        rgb.depth = 8;
        rgb.maxThreads = 1;
        result = avifRGBImageAllocatePixels(&rgb);
        if (result != AVIF_RESULT_OK) fail(std::string("avifRGBImageAllocatePixels failed: ") + avifResultToString(result));
        rgb_allocated = true;
        result = avifImageYUVToRGB(decoder->image, &rgb);
        if (result != AVIF_RESULT_OK) fail(std::string("avifImageYUVToRGB failed: ") + avifResultToString(result));
        auto t2 = Clock::now();

        if (rgb_allocated) avifRGBImageFreePixels(&rgb);
        avifDecoderDestroy(decoder);
        auto t3 = Clock::now();

        if (measure) {
            setup_acc += Seconds(t1 - t0).count();
            process_acc += Seconds(t2 - t1).count();
            reset_acc += Seconds(t3 - t2).count();
        }
    };

    for (int i = 0; i < 10; ++i) {
        double a = 0.0, b = 0.0, c = 0.0;
        one_cycle(false, a, b, c);
    }

    auto tc0 = Clock::now();
    avifDecoder* created = make_decoder();
    auto tc1 = Clock::now();
    double create_time = Seconds(tc1 - tc0).count();
    avifDecoderDestroy(created);

    double setup_acc = 0.0, process_acc = 0.0, reset_acc = 0.0;
    for (int i = 0; i < opt.iterations; ++i) {
        one_cycle(true, setup_acc, process_acc, reset_acc);
    }

    auto td0 = Clock::now();
    avifDecoder* destroy_probe = make_decoder();
    avifDecoderDestroy(destroy_probe);
    auto td1 = Clock::now();
    double destroy_time = Seconds(td1 - td0).count() - create_time;
    if (destroy_time < 0.0) destroy_time = 0.0;

    std::cout << std::fixed << std::setprecision(9)
              << "CREATE_TIME:" << create_time << '\n'
              << "SETUP_TIME:" << (setup_acc / opt.iterations) << '\n'
              << "PROCESS_TIME:" << (process_acc / opt.iterations) << '\n'
              << "RESET_TIME:" << (reset_acc / opt.iterations) << '\n'
              << "DESTROY_TIME:" << destroy_time << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options opt = parse_args(argc, argv);
        if (opt.help) {
            print_help(std::cout, argv[0]);
            return 0;
        }
        std::vector<uint8_t> avif_bytes = read_all_stdin();
        if (opt.benchmark) {
            benchmark(opt, avif_bytes);
            return 0;
        }
        std::vector<uint8_t> rgb24 = decode_once(opt, avif_bytes);
        if (opt.output.has_value()) {
            write_all(rgb24, *opt.output);
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n\n";
        print_help(std::cerr, argc > 0 ? argv[0] : "libavif_decode");
        return 2;
    }
}
