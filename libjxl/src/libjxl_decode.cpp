#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <jxl/decode.h>

namespace {
using Clock = std::chrono::steady_clock;

struct Options {
    int iterations = 1;
    bool benchmark = false;
    bool output_enabled = false;
    std::string output;
    bool help = false;
};

struct Metrics {
    double create_time = 0.0;
    double setup_time = 0.0;
    double process_time = 0.0;
    double reset_time = 0.0;
    double destroy_time = 0.0;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require_status(JxlDecoderStatus status, const std::string& what) {
    if (status != JXL_DEC_SUCCESS) {
        fail(what + " failed");
    }
}

void print_help(const char* program) {
    std::cout
        << "Usage: " << program << " [--iterations N] [--benchmark] [--output PATH|-]\n\n"
        << "Reads a JPEG XL image from stdin and outputs an interleaved RGB24 image.\n\n"
        << "Options:\n"
        << "  --iterations N    Number of measured iterations when not using --output.\n"
        << "  --benchmark       Enable benchmark mode and print timing metrics.\n"
        << "  --output PATH|-   Write one decoded RGB24 payload to PATH or stdout.\n"
        << "  --help, -h        Show this help message.\n\n"
        << "Rules:\n"
        << "  * --benchmark disables --output.\n"
        << "  * --output decodes exactly once and ignores iteration benchmarking.\n"
        << "  * Benchmarking performs 10 warmup iterations and then N measured iterations.\n"
        << "  * This benchmark runs libjxl single-threaded.\n";
}

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) fail("Missing value for " + flag);
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            opt.help = true;
            return opt;
        } else if (arg == "--iterations") {
            opt.iterations = std::stoi(require_value(arg));
        } else if (arg == "--benchmark") {
            opt.benchmark = true;
        } else if (arg == "--output") {
            opt.output = require_value(arg);
            opt.output_enabled = true;
        } else {
            fail("Unknown argument: " + arg);
        }
    }
    if (opt.iterations <= 0) fail("--iterations must be a positive integer");
    if (opt.benchmark && opt.output_enabled) fail("--benchmark cannot be combined with --output");
    return opt;
}

std::vector<uint8_t> read_stdin_all() {
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(std::cin.rdbuf()), std::istreambuf_iterator<char>());
}

struct DecodeResult {
    std::vector<uint8_t> rgb;
    uint32_t width = 0;
    uint32_t height = 0;
};

DecodeResult decode_once(JxlDecoder* dec, const std::vector<uint8_t>& encoded) {
    require_status(JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE),
                   "JxlDecoderSubscribeEvents");
    JxlDecoderSetInput(dec, encoded.data(), encoded.size());
    JxlDecoderCloseInput(dec);

    const JxlPixelFormat pixel_format = {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    DecodeResult result;
    bool seen_basic_info = false;

    while (true) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(dec);
        switch (status) {
            case JXL_DEC_BASIC_INFO: {
                JxlBasicInfo info;
                require_status(JxlDecoderGetBasicInfo(dec, &info), "JxlDecoderGetBasicInfo");
                result.width = info.xsize;
                result.height = info.ysize;
                seen_basic_info = true;
                break;
            }
            case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
                size_t buffer_size = 0;
                require_status(JxlDecoderImageOutBufferSize(dec, &pixel_format, &buffer_size),
                               "JxlDecoderImageOutBufferSize");
                result.rgb.resize(buffer_size);
                require_status(JxlDecoderSetImageOutBuffer(dec, &pixel_format, result.rgb.data(), result.rgb.size()),
                               "JxlDecoderSetImageOutBuffer");
                break;
            }
            case JXL_DEC_FULL_IMAGE:
                break;
            case JXL_DEC_SUCCESS:
                if (!seen_basic_info) fail("Decoder did not provide basic info");
                return result;
            case JXL_DEC_NEED_MORE_INPUT:
                fail("Unexpected JXL_DEC_NEED_MORE_INPUT for closed input stream");
            case JXL_DEC_ERROR:
            default:
                fail("JxlDecoderProcessInput failed");
        }
    }
}

Metrics benchmark_decode(const Options& opt, const std::vector<uint8_t>& encoded) {
    Metrics m;

    auto create_start = Clock::now();
    JxlDecoder* dec = JxlDecoderCreate(nullptr);
    auto create_end = Clock::now();
    if (!dec) fail("JxlDecoderCreate failed");
    m.create_time = std::chrono::duration<double>(create_end - create_start).count();

    auto run_iteration = [&](bool measure, double& setup_acc, double& process_acc, double& reset_acc) {
        const JxlPixelFormat pixel_format = {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
        DecodeResult result;

        auto setup_start = Clock::now();
        require_status(JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE),
                       "JxlDecoderSubscribeEvents");
        JxlDecoderSetInput(dec, encoded.data(), encoded.size());
        JxlDecoderCloseInput(dec);

        bool output_buffer_set = false;
        while (!output_buffer_set) {
            const JxlDecoderStatus status = JxlDecoderProcessInput(dec);
            if (status == JXL_DEC_BASIC_INFO) {
                JxlBasicInfo info;
                require_status(JxlDecoderGetBasicInfo(dec, &info), "JxlDecoderGetBasicInfo");
                result.width = info.xsize;
                result.height = info.ysize;
            } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
                size_t buffer_size = 0;
                require_status(JxlDecoderImageOutBufferSize(dec, &pixel_format, &buffer_size),
                               "JxlDecoderImageOutBufferSize");
                result.rgb.resize(buffer_size);
                require_status(JxlDecoderSetImageOutBuffer(dec, &pixel_format, result.rgb.data(), result.rgb.size()),
                               "JxlDecoderSetImageOutBuffer");
                output_buffer_set = true;
            } else if (status == JXL_DEC_ERROR || status == JXL_DEC_NEED_MORE_INPUT) {
                fail("Decoder setup failed");
            }
        }
        auto setup_end = Clock::now();

        auto process_start = Clock::now();
        while (true) {
            const JxlDecoderStatus status = JxlDecoderProcessInput(dec);
            if (status == JXL_DEC_SUCCESS) break;
            if (status == JXL_DEC_FULL_IMAGE) continue;
            if (status == JXL_DEC_ERROR || status == JXL_DEC_NEED_MORE_INPUT) fail("Decoder processing failed");
        }
        auto process_end = Clock::now();

        auto reset_start = Clock::now();
        result.rgb.clear();
        result.rgb.shrink_to_fit();
        JxlDecoderReset(dec);
        auto reset_end = Clock::now();

        if (measure) {
            setup_acc += std::chrono::duration<double>(setup_end - setup_start).count();
            process_acc += std::chrono::duration<double>(process_end - process_start).count();
            reset_acc += std::chrono::duration<double>(reset_end - reset_start).count();
        }
    };

    double setup_acc = 0.0;
    double process_acc = 0.0;
    double reset_acc = 0.0;

    for (int i = 0; i < 10; ++i) run_iteration(false, setup_acc, process_acc, reset_acc);
    for (int i = 0; i < opt.iterations; ++i) run_iteration(true, setup_acc, process_acc, reset_acc);

    auto destroy_start = Clock::now();
    JxlDecoderDestroy(dec);
    auto destroy_end = Clock::now();
    m.destroy_time = std::chrono::duration<double>(destroy_end - destroy_start).count();
    m.setup_time = setup_acc / static_cast<double>(opt.iterations);
    m.process_time = process_acc / static_cast<double>(opt.iterations);
    m.reset_time = reset_acc / static_cast<double>(opt.iterations);
    return m;
}

void write_binary_output(const std::vector<uint8_t>& payload, const std::string& output) {
    if (output == "-") {
        std::cout.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        std::cout.flush();
        return;
    }
    std::ofstream ofs(output, std::ios::binary);
    if (!ofs) fail("Unable to open output file: " + output);
    ofs.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options opt = parse_args(argc, argv);
        if (opt.help) {
            print_help(argv[0]);
            return 0;
        }

        const auto encoded = read_stdin_all();
        if (encoded.empty()) fail("No input data received on stdin");

        if (opt.benchmark) {
            const Metrics m = benchmark_decode(opt, encoded);
            std::cout << "CREATE_TIME:" << m.create_time << '\n';
            std::cout << "SETUP_TIME:" << m.setup_time << '\n';
            std::cout << "PROCESS_TIME:" << m.process_time << '\n';
            std::cout << "RESET_TIME:" << m.reset_time << '\n';
            std::cout << "DESTROY_TIME:" << m.destroy_time << '\n';
            return 0;
        }

        JxlDecoder* dec = JxlDecoderCreate(nullptr);
        if (!dec) fail("JxlDecoderCreate failed");

        if (opt.output_enabled) {
            const auto decoded = decode_once(dec, encoded);
            JxlDecoderDestroy(dec);
            write_binary_output(decoded.rgb, opt.output);
            return 0;
        }

        for (int i = 0; i < 10; ++i) {
            (void)decode_once(dec, encoded);
            JxlDecoderReset(dec);
        }
        for (int i = 0; i < opt.iterations; ++i) {
            (void)decode_once(dec, encoded);
            JxlDecoderReset(dec);
        }
        JxlDecoderDestroy(dec);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n\n";
        print_help(argv[0]);
        return 1;
    }
}
