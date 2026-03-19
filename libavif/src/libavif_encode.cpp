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
    int width = 0;
    int height = 0;
    int iterations = 1;
    bool benchmark = false;
    std::optional<std::string> output;
    int quality = 50;
    std::string subsampling = "444";
    std::string backend = "aom";
    bool help = false;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void print_help(std::ostream& os, const char* argv0) {
    os << "libavif_encode - RGB24 -> AVIF encoder benchmark\n\n"
       << "Syntax:\n"
       << "  " << argv0 << " --width W --height H [--iterations N] --quality Q --subsampling {444|422|420} --backend {aom|rav1e|svt} [--benchmark] [--output PATH|-]\n\n"
       << "Notes:\n"
       << "  * Input image is read from stdin as raw interleaved RGB24.\n"
       << "  * If --benchmark is enabled, --output is forbidden.\n"
       << "  * If --output is supplied, benchmark/iterations are disabled and a single encode is performed.\n"
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
        } else if (arg == "--width") {
            opt.width = std::stoi(need_value("--width"));
        } else if (arg == "--height") {
            opt.height = std::stoi(need_value("--height"));
        } else if (arg == "--iterations" || arg == "-i") {
            opt.iterations = std::stoi(need_value("--iterations"));
        } else if (arg == "--benchmark" || arg == "-b") {
            opt.benchmark = true;
        } else if (arg == "--output" || arg == "-o") {
            opt.output = need_value("--output");
        } else if (arg == "--quality") {
            opt.quality = std::stoi(need_value("--quality"));
        } else if (arg == "--subsampling") {
            opt.subsampling = need_value("--subsampling");
        } else if (arg == "--backend") {
            opt.backend = need_value("--backend");
        } else {
            fail("Unknown argument: " + std::string(arg));
        }
    }

    if (opt.help) return opt;
    if (opt.width <= 0 || opt.height <= 0) fail("--width and --height must be positive integers");
    if (opt.iterations <= 0) fail("--iterations must be positive");
    if (opt.quality < 0 || opt.quality > 100) fail("--quality must be in [0,100]");
    if (opt.subsampling != "444" && opt.subsampling != "422" && opt.subsampling != "420") {
        fail("--subsampling must be one of: 444, 422, 420");
    }
    if (opt.backend != "aom" && opt.backend != "rav1e" && opt.backend != "svt") {
        fail("--backend must be one of: aom, rav1e, svt");
    }
    if (opt.output.has_value() && opt.benchmark) {
        fail("--output cannot be used together with --benchmark");
    }
    if (opt.output.has_value() && opt.iterations != 1) {
        fail("--output cannot be used together with --iterations != 1");
    }
    if (opt.backend == "svt" && opt.subsampling != "420") {
        fail("Backend 'svt' supports only 420 subsampling in this benchmark harness");
    }
    if (opt.backend == "rav1e" && opt.subsampling == "422") {
        fail("Backend 'rav1e' does not support 422 subsampling in this benchmark harness");
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

avifPixelFormat pixel_format_from_string(const std::string& s) {
    if (s == "444") return AVIF_PIXEL_FORMAT_YUV444;
    if (s == "422") return AVIF_PIXEL_FORMAT_YUV422;
    if (s == "420") return AVIF_PIXEL_FORMAT_YUV420;
    fail("Unsupported subsampling");
}

avifCodecChoice codec_choice_from_backend(const std::string& backend) {
    if (backend == "aom") return AVIF_CODEC_CHOICE_AOM;
    if (backend == "rav1e") return AVIF_CODEC_CHOICE_RAV1E;
    if (backend == "svt") return AVIF_CODEC_CHOICE_SVT;
    fail("Unsupported backend");
}

struct EncodePayload {
    std::vector<uint8_t> data;
};

EncodePayload encode_once(const Options& opt, const std::vector<uint8_t>& rgb24) {
    const std::size_t expected = static_cast<std::size_t>(opt.width) * static_cast<std::size_t>(opt.height) * 3u;
    if (rgb24.size() != expected) {
        std::ostringstream oss;
        oss << "Expected " << expected << " RGB bytes on stdin, got " << rgb24.size();
        fail(oss.str());
    }

    avifEncoder* encoder = avifEncoderCreate();
    if (!encoder) fail("avifEncoderCreate failed");
    encoder->codecChoice = codec_choice_from_backend(opt.backend);
    encoder->maxThreads = 1;
    encoder->quality = opt.quality;
    encoder->qualityAlpha = opt.quality;
    encoder->speed = 10;

    avifImage* image = avifImageCreate(opt.width, opt.height, 8, pixel_format_from_string(opt.subsampling));
    if (!image) {
        avifEncoderDestroy(encoder);
        fail("avifImageCreate failed");
    }
    image->yuvRange = AVIF_RANGE_FULL;

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = AVIF_RGB_FORMAT_RGB;
    rgb.depth = 8;
    rgb.maxThreads = 1;
    rgb.rowBytes = static_cast<uint32_t>(opt.width * 3);
    rgb.pixels = const_cast<uint8_t*>(rgb24.data());
    avifResult result = avifImageRGBToYUV(image, &rgb);
    if (result != AVIF_RESULT_OK) {
        avifImageDestroy(image);
        avifEncoderDestroy(encoder);
        fail(std::string("avifImageRGBToYUV failed: ") + avifResultToString(result));
    }

    avifRWData output = AVIF_DATA_EMPTY;
    result = avifEncoderWrite(encoder, image, &output);
    if (result != AVIF_RESULT_OK) {
        avifRWDataFree(&output);
        avifImageDestroy(image);
        avifEncoderDestroy(encoder);
        fail(std::string("avifEncoderWrite failed: ") + avifResultToString(result));
    }

    EncodePayload payload;
    payload.data.assign(output.data, output.data + output.size);

    avifRWDataFree(&output);
    avifImageDestroy(image);
    avifEncoderDestroy(encoder);
    return payload;
}

void benchmark(const Options& opt, const std::vector<uint8_t>& rgb24) {
    const std::size_t expected = static_cast<std::size_t>(opt.width) * static_cast<std::size_t>(opt.height) * 3u;
    if (rgb24.size() != expected) {
        std::ostringstream oss;
        oss << "Expected " << expected << " RGB bytes on stdin, got " << rgb24.size();
        fail(oss.str());
    }

    auto make_encoder = [&]() -> avifEncoder* {
        avifEncoder* e = avifEncoderCreate();
        if (!e) fail("avifEncoderCreate failed");
        e->codecChoice = codec_choice_from_backend(opt.backend);
        e->maxThreads = 1;
        e->quality = opt.quality;
        e->qualityAlpha = opt.quality;
        e->speed = 10;
        return e;
    };

    auto one_cycle = [&](bool measure, double& setup_acc, double& process_acc, double& reset_acc) {
        avifEncoder* encoder = make_encoder();
        avifImage* image = nullptr;
        avifRWData output = AVIF_DATA_EMPTY;

        auto t0 = Clock::now();
        image = avifImageCreate(opt.width, opt.height, 8, pixel_format_from_string(opt.subsampling));
        if (!image) fail("avifImageCreate failed");
        image->yuvRange = AVIF_RANGE_FULL;
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, image);
        rgb.format = AVIF_RGB_FORMAT_RGB;
        rgb.depth = 8;
        rgb.maxThreads = 1;
        rgb.rowBytes = static_cast<uint32_t>(opt.width * 3);
        rgb.pixels = const_cast<uint8_t*>(rgb24.data());
        avifResult result = avifImageRGBToYUV(image, &rgb);
        if (result != AVIF_RESULT_OK) fail(std::string("avifImageRGBToYUV failed: ") + avifResultToString(result));
        auto t1 = Clock::now();

        result = avifEncoderWrite(encoder, image, &output);
        if (result != AVIF_RESULT_OK) fail(std::string("avifEncoderWrite failed: ") + avifResultToString(result));
        auto t2 = Clock::now();

        avifRWDataFree(&output);
        avifImageDestroy(image);
        avifEncoderDestroy(encoder);
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
    avifEncoder* created = make_encoder();
    auto tc1 = Clock::now();
    double create_time = Seconds(tc1 - tc0).count();
    avifEncoderDestroy(created);

    double setup_acc = 0.0, process_acc = 0.0, reset_acc = 0.0;
    for (int i = 0; i < opt.iterations; ++i) {
        one_cycle(true, setup_acc, process_acc, reset_acc);
    }

    auto td0 = Clock::now();
    avifEncoder* destroy_probe = make_encoder();
    avifEncoderDestroy(destroy_probe);
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
        std::vector<uint8_t> rgb24 = read_all_stdin();
        if (opt.benchmark) {
            benchmark(opt, rgb24);
            return 0;
        }
        EncodePayload payload = encode_once(opt, rgb24);
        if (opt.output.has_value()) {
            write_all(payload.data, *opt.output);
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n\n";
        print_help(std::cerr, argc > 0 ? argv[0] : "libavif_encode");
        return 2;
    }
}
