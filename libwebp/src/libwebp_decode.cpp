#include <webp/decode.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  int iterations = 1;
  bool benchmark = false;
  std::optional<std::string> output;
  bool help = false;
};

struct Timings {
  double create_time = 0.0;
  double setup_time = 0.0;
  double process_time = 0.0;
  double reset_time = 0.0;
  double destroy_time = 0.0;
};

struct DecoderContext {
  // Intentionally minimal: libwebp decode is mostly configuration-driven.
  int placeholder = 0;
};

std::string HelpText(const char* argv0) {
  std::ostringstream oss;
  oss << "Usage:\n"
      << "  " << argv0
      << " [--iterations N] [--benchmark] [--output PATH|-]\n\n"
      << "Input:\n"
      << "  Encoded WebP bitstream is read from stdin.\n\n"
      << "Modes:\n"
      << "  Benchmark mode : requires --iterations N, ignores --output, performs 10 warmup iterations.\n"
      << "  Output mode    : use --output PATH or --output -, runs exactly one decode.\n\n"
      << "Flags:\n"
      << "  --iterations N  Number of measured iterations (benchmark mode).\n"
      << "  --benchmark     Print benchmark metrics to stdout.\n"
      << "  --output PATH|- Write decoded RGB24 to PATH or stdout when '-'.\n"
      << "  --help          Show this help message.\n\n"
      << "Benchmark output format:\n"
      << "  CREATE_TIME:value\n"
      << "  SETUP_TIME:value\n"
      << "  PROCESS_TIME:value\n"
      << "  RESET_TIME:value\n"
      << "  DESTROY_TIME:value\n";
  return oss.str();
}

bool ParseInt(std::string_view s, int* out) {
  try {
    size_t idx = 0;
    const int value = std::stoi(std::string(s), &idx);
    if (idx != s.size()) return false;
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseArgs(int argc, char** argv, Options* opt, std::string* error) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    auto need_value = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        *error = std::string("missing value for ") + name;
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--help" || arg == "-h") {
      opt->help = true;
      return true;
    } else if (arg == "--iterations") {
      const char* value = need_value("--iterations");
      if (!value || !ParseInt(value, &opt->iterations)) {
        *error = "invalid integer for --iterations";
        return false;
      }
    } else if (arg == "--benchmark") {
      opt->benchmark = true;
    } else if (arg == "--output") {
      const char* value = need_value("--output");
      if (!value) return false;
      opt->output = std::string(value);
    } else {
      *error = std::string("unknown argument: ") + std::string(arg);
      return false;
    }
  }

  if (opt->iterations <= 0) {
    *error = "--iterations must be > 0";
    return false;
  }
  if (opt->benchmark && opt->output.has_value()) {
    *error = "--benchmark and --output are mutually exclusive";
    return false;
  }
  if (opt->output.has_value() && opt->iterations != 1) {
    *error = "--output disables --iterations; use the default single iteration";
    return false;
  }
  return true;
}

std::vector<std::uint8_t> ReadAllStdin() {
  std::istreambuf_iterator<char> begin(std::cin.rdbuf());
  std::istreambuf_iterator<char> end;
  std::vector<std::uint8_t> data;
  for (auto it = begin; it != end; ++it) {
    data.push_back(static_cast<std::uint8_t>(*it));
  }
  return data;
}

bool WriteAll(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string* error) {
  if (path == "-") {
    std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(std::cout);
  }
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) {
    *error = "failed to open output file: " + path;
    return false;
  }
  ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!ofs) {
    *error = "failed to write output file: " + path;
    return false;
  }
  return true;
}

double SecondsSince(const Clock::time_point& start, const Clock::time_point& end) {
  return std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
}

bool DecodeOnce(const std::vector<std::uint8_t>& bitstream,
                std::vector<std::uint8_t>* rgb,
                int* out_width,
                int* out_height,
                double* setup_time,
                double* process_time,
                double* reset_time,
                std::string* error) {
  WebPDecoderConfig config;
  const auto t0 = Clock::now();
  if (!WebPInitDecoderConfig(&config)) {
    *error = "WebPInitDecoderConfig failed";
    return false;
  }
  if (WebPGetFeatures(bitstream.data(), bitstream.size(), &config.input) != VP8_STATUS_OK) {
    *error = "WebPGetFeatures failed";
    return false;
  }
  config.options.use_threads = 0;  // single thread
  config.output.colorspace = MODE_RGB;
  const auto t1 = Clock::now();

  const auto t2 = Clock::now();
  const VP8StatusCode status = WebPDecode(bitstream.data(), bitstream.size(), &config);
  const auto t3 = Clock::now();
  if (status != VP8_STATUS_OK) {
    WebPFreeDecBuffer(&config.output);
    *error = "WebPDecode failed with status code " + std::to_string(static_cast<int>(status));
    return false;
  }

  const int width = config.input.width;
  const int height = config.input.height;
  const std::size_t size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u;
  rgb->assign(config.output.u.RGBA.rgba, config.output.u.RGBA.rgba + size);
  *out_width = width;
  *out_height = height;

  const auto t4 = Clock::now();
  WebPFreeDecBuffer(&config.output);
  const auto t5 = Clock::now();

  *setup_time += SecondsSince(t0, t1);
  *process_time += SecondsSince(t2, t3);
  *reset_time += SecondsSince(t4, t5);
  return true;
}

bool RunBenchmark(const Options& opt,
                  const std::vector<std::uint8_t>& bitstream,
                  Timings* timings,
                  std::string* error) {
  DecoderContext ctx;
  (void)ctx;
  const auto t0 = Clock::now();
  const auto t1 = Clock::now();
  timings->create_time = SecondsSince(t0, t1);

  std::vector<std::uint8_t> sink;
  int width = 0, height = 0;
  for (int i = 0; i < 10; ++i) {
    double setup = 0.0, process = 0.0, reset = 0.0;
    if (!DecodeOnce(bitstream, &sink, &width, &height, &setup, &process, &reset, error)) {
      return false;
    }
  }

  for (int i = 0; i < opt.iterations; ++i) {
    std::vector<std::uint8_t> output;
    if (!DecodeOnce(bitstream, &output, &width, &height,
                    &timings->setup_time, &timings->process_time, &timings->reset_time, error)) {
      return false;
    }
  }

  timings->setup_time /= static_cast<double>(opt.iterations);
  timings->process_time /= static_cast<double>(opt.iterations);
  timings->reset_time /= static_cast<double>(opt.iterations);

  const auto t2 = Clock::now();
  const auto t3 = Clock::now();
  timings->destroy_time = SecondsSince(t2, t3);
  return true;
}

bool RunOutputMode(const std::vector<std::uint8_t>& bitstream,
                   const std::string& output_path,
                   std::string* error) {
  std::vector<std::uint8_t> rgb;
  int width = 0, height = 0;
  double setup = 0.0, process = 0.0, reset = 0.0;
  if (!DecodeOnce(bitstream, &rgb, &width, &height, &setup, &process, &reset, error)) {
    return false;
  }
  return WriteAll(output_path, rgb, error);
}

}  // namespace

int main(int argc, char** argv) {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  Options opt;
  std::string error;
  if (!ParseArgs(argc, argv, &opt, &error)) {
    std::cerr << "Error: " << error << "\n\n" << HelpText(argv[0]);
    return EXIT_FAILURE;
  }
  if (opt.help) {
    std::cout << HelpText(argv[0]);
    return EXIT_SUCCESS;
  }

  const std::vector<std::uint8_t> bitstream = ReadAllStdin();
  if (bitstream.empty()) {
    std::cerr << "Error: stdin is empty\n";
    return EXIT_FAILURE;
  }

  if (opt.benchmark) {
    Timings timings;
    if (!RunBenchmark(opt, bitstream, &timings, &error)) {
      std::cerr << "Error: " << error << "\n";
      return EXIT_FAILURE;
    }
    std::cout << std::fixed << std::setprecision(9)
              << "CREATE_TIME:" << timings.create_time << '\n'
              << "SETUP_TIME:" << timings.setup_time << '\n'
              << "PROCESS_TIME:" << timings.process_time << '\n'
              << "RESET_TIME:" << timings.reset_time << '\n'
              << "DESTROY_TIME:" << timings.destroy_time << '\n';
    return EXIT_SUCCESS;
  }

  if (opt.output.has_value()) {
    if (!RunOutputMode(bitstream, *opt.output, &error)) {
      std::cerr << "Error: " << error << "\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  std::cerr << HelpText(argv[0]);
  return EXIT_FAILURE;
}
