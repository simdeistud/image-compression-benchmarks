#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <jxl/color_encoding.h>
#include <jxl/encode.h>

namespace {
using Clock = std::chrono::steady_clock;

struct Options {
    int width = 0;
    int height = 0;
    int iterations = 1;
    double quality = 90.0;
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

void require_status(JxlEncoderStatus status, const std::string& what) {
    if (status != JXL_ENC_SUCCESS) {
        fail(what + " failed");
    }
}

void print_help(const char* program) {
    std::cout
        << "Usage: " << program << " --width W --height H [--iterations N] [--quality Q] [--benchmark] [--output PATH|-]\n\n"
        << "Reads an interleaved RGB24 image from stdin and encodes it to JPEG XL.\n\n"
        << "Options:\n"
        << "  --width W         Input width in pixels (required).\n"
        << "  --height H        Input height in pixels (required).\n"
        << "  --iterations N    Number of measured iterations when not using --output.\n"
        << "  --quality Q       Quality value in [0,100]. Higher means better quality.\n"
        << "  --benchmark       Enable benchmark mode and print timing metrics.\n"
        << "  --output PATH|-   Write one encoded payload to PATH or stdout.\n"
        << "  --help, -h        Show this help message.\n\n"
        << "Rules:\n"
        << "  * --benchmark disables --output.\n"
        << "  * --output encodes exactly once and ignores iteration benchmarking.\n"
        << "  * Benchmarking performs 10 warmup iterations and then N measured iterations.\n"
        << "  * This benchmark uses the libjxl fastest documented settings: effort=1, faster_decoding=4, single-threaded execution.\n";
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
        } else if (arg == "--width") {
            opt.width = std::stoi(require_value(arg));
        } else if (arg == "--height") {
            opt.height = std::stoi(require_value(arg));
        } else if (arg == "--iterations") {
            opt.iterations = std::stoi(require_value(arg));
        } else if (arg == "--quality") {
            opt.quality = std::stod(require_value(arg));
        } else if (arg == "--benchmark") {
            opt.benchmark = true;
        } else if (arg == "--output") {
            opt.output = require_value(arg);
            opt.output_enabled = true;
        } else {
            fail("Unknown argument: " + arg);
        }
    }

    if (opt.width <= 0 || opt.height <= 0) fail("--width and --height must be positive integers");
    if (opt.iterations <= 0) fail("--iterations must be a positive integer");
    if (opt.quality < 0.0 || opt.quality > 100.0) fail("--quality must be in [0, 100]");
    if (opt.benchmark && opt.output_enabled) fail("--benchmark cannot be combined with --output");
    return opt;
}

std::vector<uint8_t> read_stdin_all() {
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(std::cin.rdbuf()), std::istreambuf_iterator<char>());
}

float quality_to_distance(double quality) {
    if (quality >= 100.0) return 0.0f;
    if (quality >= 90.0) return static_cast<float>((100.0 - quality) * 0.10);
    if (quality >= 30.0) return static_cast<float>(0.1 + (100.0 - quality) * 0.09);
    if (quality > 0.0) return static_cast<float>(15.0 + (59.0 * quality - 4350.0) * quality / 9000.0);
    return 15.0f;
}

void configure_encoder(JxlEncoder* enc, JxlEncoderFrameSettings** frame_settings, const Options& opt,
                       const std::vector<uint8_t>& rgb) {
    JxlBasicInfo basic_info;
    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = static_cast<uint32_t>(opt.width);
    basic_info.ysize = static_cast<uint32_t>(opt.height);
    basic_info.bits_per_sample = 8;
    basic_info.exponent_bits_per_sample = 0;
    basic_info.num_color_channels = 3;
    basic_info.num_extra_channels = 0;
    basic_info.uses_original_profile = JXL_FALSE;

    require_status(JxlEncoderSetBasicInfo(enc, &basic_info), "JxlEncoderSetBasicInfo");
    require_status(JxlEncoderUseContainer(enc, JXL_FALSE), "JxlEncoderUseContainer");

    JxlColorEncoding color_encoding;
    JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
    require_status(JxlEncoderSetColorEncoding(enc, &color_encoding), "JxlEncoderSetColorEncoding");

    *frame_settings = JxlEncoderFrameSettingsCreate(enc, nullptr);
    if (*frame_settings == nullptr) fail("JxlEncoderFrameSettingsCreate failed");

    require_status(JxlEncoderFrameSettingsSetOption(*frame_settings, JXL_ENC_FRAME_SETTING_EFFORT, 1),
                   "JxlEncoderFrameSettingsSetOption(EFFORT)");
    require_status(JxlEncoderFrameSettingsSetOption(*frame_settings, JXL_ENC_FRAME_SETTING_DECODING_SPEED, 4),
                   "JxlEncoderFrameSettingsSetOption(DECODING_SPEED)");

    const float distance = quality_to_distance(opt.quality);
    require_status(JxlEncoderSetFrameDistance(*frame_settings, distance), "JxlEncoderSetFrameDistance");

    const JxlPixelFormat pixel_format = {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    require_status(JxlEncoderAddImageFrame(*frame_settings, &pixel_format, rgb.data(), rgb.size()),
                   "JxlEncoderAddImageFrame");
    JxlEncoderCloseInput(enc);
}

std::vector<uint8_t> process_encoder_output(JxlEncoder* enc) {
    std::vector<uint8_t> output(4096);
    uint8_t* next_out = output.data();
    size_t avail_out = output.size();

    while (true) {
        const JxlEncoderStatus status = JxlEncoderProcessOutput(enc, &next_out, &avail_out);
        if (status == JXL_ENC_NEED_MORE_OUTPUT) {
            const std::size_t used = static_cast<std::size_t>(next_out - output.data());
            output.resize(output.size() * 2);
            next_out = output.data() + used;
            avail_out = output.size() - used;
            continue;
        }
        if (status == JXL_ENC_SUCCESS) {
            output.resize(static_cast<std::size_t>(next_out - output.data()));
            return output;
        }
        fail("JxlEncoderProcessOutput failed");
    }
}

std::vector<uint8_t> encode_once(JxlEncoder* enc, const Options& opt, const std::vector<uint8_t>& rgb) {
    JxlEncoderFrameSettings* frame_settings = nullptr;
    configure_encoder(enc, &frame_settings, opt, rgb);
    return process_encoder_output(enc);
}

Metrics benchmark_encode(const Options& opt, const std::vector<uint8_t>& rgb) {
    Metrics m;

    auto create_start = Clock::now();
    JxlEncoder* enc = JxlEncoderCreate(nullptr);
    auto create_end = Clock::now();
    if (!enc) fail("JxlEncoderCreate failed");
    m.create_time = std::chrono::duration<double>(create_end - create_start).count();

    auto run_iteration = [&](bool measure, double& setup_acc, double& process_acc, double& reset_acc) {
        JxlEncoderFrameSettings* frame_settings = nullptr;

        auto setup_start = Clock::now();
        configure_encoder(enc, &frame_settings, opt, rgb);
        auto setup_end = Clock::now();

        auto process_start = Clock::now();
        std::vector<uint8_t> encoded = process_encoder_output(enc);
        auto process_end = Clock::now();

        auto reset_start = Clock::now();
        encoded.clear();
        encoded.shrink_to_fit();
        JxlEncoderReset(enc);
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
    JxlEncoderDestroy(enc);
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

        const auto rgb = read_stdin_all();
        const std::size_t expected_size = static_cast<std::size_t>(opt.width) * static_cast<std::size_t>(opt.height) * 3U;
        if (rgb.size() != expected_size) {
            fail("Input size mismatch: expected " + std::to_string(expected_size) + " bytes, got " + std::to_string(rgb.size()));
        }

        if (opt.benchmark) {
            const Metrics m = benchmark_encode(opt, rgb);
            std::cout << "CREATE_TIME:" << m.create_time << '\n';
            std::cout << "SETUP_TIME:" << m.setup_time << '\n';
            std::cout << "PROCESS_TIME:" << m.process_time << '\n';
            std::cout << "RESET_TIME:" << m.reset_time << '\n';
            std::cout << "DESTROY_TIME:" << m.destroy_time << '\n';
            return 0;
        }

        JxlEncoder* enc = JxlEncoderCreate(nullptr);
        if (!enc) fail("JxlEncoderCreate failed");

        if (opt.output_enabled) {
            const auto encoded = encode_once(enc, opt, rgb);
            JxlEncoderDestroy(enc);
            write_binary_output(encoded, opt.output);
            return 0;
        }

        for (int i = 0; i < 10; ++i) {
            (void)encode_once(enc, opt, rgb);
            JxlEncoderReset(enc);
        }
        for (int i = 0; i < opt.iterations; ++i) {
            (void)encode_once(enc, opt, rgb);
            JxlEncoderReset(enc);
        }
        JxlEncoderDestroy(enc);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n\n";
        print_help(argv[0]);
        return 1;
    }
}
