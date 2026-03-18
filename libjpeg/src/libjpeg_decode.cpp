#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <jpeglib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
using clock_type = std::chrono::steady_clock;
using seconds_d = std::chrono::duration<double>;

struct Args {
    int width = 0;
    int height = 0;
    int iterations = 1;
    bool benchmark = false;
    std::optional<std::string> output;
    std::string dct = "int";
};

struct Metrics {
    double create_time = 0.0;
    double setup_time = 0.0;
    double process_time = 0.0;
    double reset_time = 0.0;
    double destroy_time = 0.0;
};

struct JpegErrorManager {
    jpeg_error_mgr pub{};
    jmp_buf jump_buffer{};
    char message[JMSG_LENGTH_MAX]{};
};

extern "C" void jpeg_error_exit(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<JpegErrorManager*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, err->message);
    longjmp(err->jump_buffer, 1);
}

void set_binary_stdio() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string_view tok(argv[i]);
        auto need_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                fail(std::string("missing value for ") + name);
            }
            return argv[++i];
        };

        if (tok == "-w") {
            args.width = std::stoi(need_value("-w"));
        } else if (tok == "-h") {
            args.height = std::stoi(need_value("-h"));
        } else if (tok == "-i") {
            args.iterations = std::stoi(need_value("-i"));
        } else if (tok == "-b") {
            args.benchmark = true;
        } else if (tok == "-o") {
            args.output = need_value("-o");
        } else if (tok == "-dct") {
            args.dct = need_value("-dct");
        } else {
            fail(std::string("unknown argument: ") + std::string(tok));
        }
    }

    if (args.width <= 0 || args.height <= 0) {
        fail("-w and -h must be positive");
    }
    if (args.iterations <= 0) {
        fail("-i must be positive");
    }
    if (!(args.dct == "fast" || args.dct == "float" || args.dct == "int")) {
        fail("-dct must be one of: fast, float, int");
    }
    if (args.output.has_value() && args.benchmark) {
        fail("-o cannot be used together with -b");
    }
    if (args.output.has_value() && args.iterations != 1) {
        fail("-o cannot be used together with -i values different from 1");
    }
    return args;
}

std::vector<unsigned char> read_all_stdin() {
    std::vector<unsigned char> data;
    constexpr std::size_t chunk_size = 1 << 20;
    std::vector<char> buffer(chunk_size);
    while (std::cin) {
        std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = std::cin.gcount();
        if (got > 0) {
            data.insert(data.end(), buffer.begin(), buffer.begin() + got);
        }
    }
    return data;
}

void write_all(const std::vector<unsigned char>& data, const std::optional<std::string>& output) {
    if (!output.has_value() || *output == "-") {
        std::cout.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!std::cout) {
            fail("failed to write to stdout");
        }
        return;
    }

    std::ofstream ofs(*output, std::ios::binary);
    if (!ofs) {
        fail("failed to open output file: " + *output);
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!ofs) {
        fail("failed to write output file: " + *output);
    }
}

J_DCT_METHOD to_dct_method(const std::string& dct) {
    if (dct == "fast") {
        return JDCT_IFAST;
    }
    if (dct == "float") {
        return JDCT_FLOAT;
    }
    return JDCT_ISLOW;
}

std::vector<unsigned char> decode_once(j_decompress_ptr dinfo, const Args& args, const std::vector<unsigned char>& jpeg_bytes, double* setup_time, double* process_time, double* reset_time) {
    const auto setup_start = clock_type::now();
    jpeg_mem_src(dinfo, const_cast<unsigned char*>(jpeg_bytes.data()), static_cast<unsigned long>(jpeg_bytes.size()));
    jpeg_read_header(dinfo, TRUE);
    dinfo->dct_method = to_dct_method(args.dct);
    dinfo->out_color_space = JCS_RGB;
    jpeg_start_decompress(dinfo);
    const auto setup_end = clock_type::now();

    if (static_cast<int>(dinfo->output_width) != args.width || static_cast<int>(dinfo->output_height) != args.height) {
        fail("decoded image dimensions do not match provided -w/-h");
    }
    if (dinfo->output_components != 3) {
        fail("decoded image is not RGB24 after conversion");
    }

    std::vector<unsigned char> rgb(static_cast<std::size_t>(args.width) * static_cast<std::size_t>(args.height) * 3);

    const auto process_start = clock_type::now();
    while (dinfo->output_scanline < dinfo->output_height) {
        JSAMPROW row = &rgb[dinfo->output_scanline * static_cast<std::size_t>(args.width) * 3];
        jpeg_read_scanlines(dinfo, &row, 1);
    }
    jpeg_finish_decompress(dinfo);
    const auto process_end = clock_type::now();

    const auto reset_start = clock_type::now();
    jpeg_abort_decompress(dinfo);
    const auto reset_end = clock_type::now();

    if (setup_time) *setup_time += seconds_d(setup_end - setup_start).count();
    if (process_time) *process_time += seconds_d(process_end - process_start).count();
    if (reset_time) *reset_time += seconds_d(reset_end - reset_start).count();
    return rgb;
}

void print_metrics(const Metrics& m) {
    std::cout.setf(std::ios::fmtflags(0), std::ios::floatfield);
    std::cout.precision(9);
    std::cout << "CREATE_TIME:" << m.create_time << '\n';
    std::cout << "SETUP_TIME:" << m.setup_time << '\n';
    std::cout << "PROCESS_TIME:" << m.process_time << '\n';
    std::cout << "RESET_TIME:" << m.reset_time << '\n';
    std::cout << "DESTROY_TIME:" << m.destroy_time << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        set_binary_stdio();
        const Args args = parse_args(argc, argv);
        const auto jpeg_bytes = read_all_stdin();
        if (jpeg_bytes.empty()) {
            fail("stdin is empty");
        }

        JpegErrorManager jerr;
        jpeg_decompress_struct dinfo{};
        dinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = jpeg_error_exit;

        if (setjmp(jerr.jump_buffer)) {
            throw std::runtime_error(std::string("libjpeg error: ") + jerr.message);
        }

        Metrics metrics;
        {
            const auto t0 = clock_type::now();
            jpeg_create_decompress(&dinfo);
            const auto t1 = clock_type::now();
            metrics.create_time = seconds_d(t1 - t0).count();
        }

        std::vector<unsigned char> last_output;
        const int warmup_iterations = (args.benchmark || args.iterations > 1) ? 10 : 0;
        for (int i = 0; i < warmup_iterations; ++i) {
            (void)decode_once(&dinfo, args, jpeg_bytes, nullptr, nullptr, nullptr);
        }

        if (args.benchmark) {
            for (int i = 0; i < args.iterations; ++i) {
                last_output = decode_once(&dinfo, args, jpeg_bytes, &metrics.setup_time, &metrics.process_time, &metrics.reset_time);
            }
            metrics.setup_time /= static_cast<double>(args.iterations);
            metrics.process_time /= static_cast<double>(args.iterations);
            metrics.reset_time /= static_cast<double>(args.iterations);
            const auto t0 = clock_type::now();
            jpeg_destroy_decompress(&dinfo);
            const auto t1 = clock_type::now();
            metrics.destroy_time = seconds_d(t1 - t0).count();
            print_metrics(metrics);
            return 0;
        }

        for (int i = 0; i < args.iterations; ++i) {
            last_output = decode_once(&dinfo, args, jpeg_bytes, nullptr, nullptr, nullptr);
        }

        const auto t0 = clock_type::now();
        jpeg_destroy_decompress(&dinfo);
        const auto t1 = clock_type::now();
        metrics.destroy_time = seconds_d(t1 - t0).count();

        write_all(last_output, args.output);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
