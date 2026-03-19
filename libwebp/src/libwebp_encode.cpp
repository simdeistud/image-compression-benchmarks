#include <webp/encode.h>

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
  int width = 0;
  int height = 0;
  int iterations = 1;
  float quality = 75.0f;
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

struct EncoderContext {
  WebPConfig config{};
};

std::string HelpText(const char* argv0) {
  std::ostringstream oss;
  oss << "Usage:\n"
      << "  " << argv0
      << " --width W --height H [--iterations N] [--quality Q] [--benchmark] [--output PATH|-]\n\n"
      << "Input:\n"
      << "  Raw interleaved RGB24 image is read from stdin.\n\n"
      << "Modes:\n"
      << "  Benchmark mode : requires --iterations N, ignores --output, performs 10 warmup iterations.\n"
      << "  Output mode    : use --output PATH or --output -, runs exactly one encode.\n\n"
      << "Flags:\n"
      << "  --width W       Input width in pixels (required).\n"
      << "  --height H      Input height in pixels (required).\n"
      << "  --iterations N  Number of measured iterations (benchmark mode).\n"
      << "  --quality Q     Lossy quality in [0, 100]. Default: 75.\n"
      << "  --benchmark     Print benchmark metrics to stdout.\n"
      << "  --output PATH|- Write encoded WebP to PATH or stdout when '-'.\n"
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

bool ParseFloat(std::string_view s, float* out) {
  try {
    size_t idx = 0;
    const float value = std::stof(std::string(s), &idx);
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
    } else if (arg == "--width") {
      const char* value = need_value("--width");
      if (!value || !ParseInt(value, &opt->width)) {
        *error = "invalid integer for --width";
        return false;
      }
    } else if (arg == "--height") {
      const char* value = need_value("--height");
      if (!value || !ParseInt(value, &opt->height)) {
        *error = "invalid integer for --height";
        return false;
      }
    } else if (arg == "--iterations") {
      const char* value = need_value("--iterations");
      if (!value || !ParseInt(value, &opt->iterations)) {
        *error = "invalid integer for --iterations";
        return false;
      }
    } else if (arg == "--quality") {
      const char* value = need_value("--quality");
      if (!value || !ParseFloat(value, &opt->quality)) {
        *error = "invalid float for --quality";
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

  if (opt->width <= 0 || opt->height <= 0) {
    *error = "--width and --height are required and must be > 0";
    return false;
  }
  if (opt->iterations <= 0) {
    *error = "--iterations must be > 0";
    return false;
  }
  if (opt->quality < 0.0f || opt->quality > 100.0f) {
    *error = "--quality must be in [0, 100]";
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

bool InitEncoderContext(float quality, EncoderContext* ctx) {
  if (!WebPConfigPreset(&ctx->config, WEBP_PRESET_DEFAULT, quality)) return false;
  ctx->config.lossless = 0;
  ctx->config.quality = quality;
  ctx->config.method = 0;              // fastest
  ctx->config.image_hint = WEBP_HINT_DEFAULT;
  ctx->config.target_size = 0;
  ctx->config.target_PSNR = 0.0f;
  ctx->config.segments = 1;
  ctx->config.sns_strength = 0;
  ctx->config.filter_strength = 0;
  ctx->config.filter_sharpness = 0;
  ctx->config.filter_type = 0;
  ctx->config.autofilter = 0;
  ctx->config.alpha_compression = 0;
  ctx->config.alpha_filtering = 0;
  ctx->config.alpha_quality = 0;
  ctx->config.pass = 1;
  ctx->config.show_compressed = 0;
  ctx->config.preprocessing = 0;
  ctx->config.partitions = 0;
  ctx->config.partition_limit = 0;
  ctx->config.use_sharp_yuv = 0;
  return WebPValidateConfig(&ctx->config) != 0;
}

bool EncodeOnce(const EncoderContext& ctx,
                const std::vector<std::uint8_t>& rgb,
                int width,
                int height,
                std::vector<std::uint8_t>* out,
                double* setup_time,
                double* process_time,
                double* reset_time,
                std::string* error) {
  WebPPicture picture;
  WebPMemoryWriter writer;

  const auto t0 = Clock::now();
  if (!WebPPictureInit(&picture)) {
    *error = "WebPPictureInit failed";
    return false;
  }
  picture.width = width;
  picture.height = height;
  WebPMemoryWriterInit(&writer);
  picture.writer = WebPMemoryWrite;
  picture.custom_ptr = &writer;
  if (!WebPPictureImportRGB(&picture, rgb.data(), width * 3)) {
    WebPPictureFree(&picture);
    *error = "WebPPictureImportRGB failed";
    return false;
  }
  const auto t1 = Clock::now();

  const auto t2 = Clock::now();
  const int ok = WebPEncode(&ctx.config, &picture);
  const auto t3 = Clock::now();
  if (!ok) {
    WebPPictureFree(&picture);
    WebPMemoryWriterClear(&writer);
    *error = "WebPEncode failed with error code " + std::to_string(static_cast<int>(picture.error_code));
    return false;
  }

  out->assign(writer.mem, writer.mem + writer.size);

  const auto t4 = Clock::now();
  WebPPictureFree(&picture);
  WebPMemoryWriterClear(&writer);
  const auto t5 = Clock::now();

  *setup_time += SecondsSince(t0, t1);
  *process_time += SecondsSince(t2, t3);
  *reset_time += SecondsSince(t4, t5);
  return true;
}

bool RunBenchmark(const Options& opt,
                  const std::vector<std::uint8_t>& rgb,
                  Timings* timings,
                  std::string* error) {
  EncoderContext ctx;
  const auto t0 = Clock::now();
  if (!InitEncoderContext(opt.quality, &ctx)) {
    *error = "failed to initialize encoder configuration";
    return false;
  }
  const auto t1 = Clock::now();
  timings->create_time = SecondsSince(t0, t1);

  std::vector<std::uint8_t> sink;
  for (int i = 0; i < 10; ++i) {
    double setup = 0.0, process = 0.0, reset = 0.0;
    if (!EncodeOnce(ctx, rgb, opt.width, opt.height, &sink, &setup, &process, &reset, error)) {
      return false;
    }
  }

  for (int i = 0; i < opt.iterations; ++i) {
    std::vector<std::uint8_t> output;
    if (!EncodeOnce(ctx, rgb, opt.width, opt.height, &output,
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

bool RunOutputMode(const Options& opt,
                   const std::vector<std::uint8_t>& rgb,
                   std::string* error) {
  EncoderContext ctx;
  if (!InitEncoderContext(opt.quality, &ctx)) {
    *error = "failed to initialize encoder configuration";
    return false;
  }
  std::vector<std::uint8_t> output;
  double setup = 0.0, process = 0.0, reset = 0.0;
  if (!EncodeOnce(ctx, rgb, opt.width, opt.height, &output, &setup, &process, &reset, error)) {
    return false;
  }
  return WriteAll(*opt.output, output, error);
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

  const std::vector<std::uint8_t> rgb = ReadAllStdin();
  const std::size_t expected_size = static_cast<std::size_t>(opt.width) * static_cast<std::size_t>(opt.height) * 3u;
  if (rgb.size() != expected_size) {
    std::cerr << "Error: stdin size mismatch, expected " << expected_size << " bytes for RGB24 but got " << rgb.size() << " bytes\n";
    return EXIT_FAILURE;
  }

  if (opt.benchmark) {
    Timings timings;
    if (!RunBenchmark(opt, rgb, &timings, &error)) {
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
    if (!RunOutputMode(opt, rgb, &error)) {
      std::cerr << "Error: " << error << "\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  std::cerr << HelpText(argv[0]);
  return EXIT_FAILURE;
}
