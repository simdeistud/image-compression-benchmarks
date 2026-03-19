#!/usr/bin/env python3
import argparse
import csv
import shlex
import subprocess
import sys
from pathlib import Path

import numpy as np
from skimage.metrics import peak_signal_noise_ratio, structural_similarity

ITERATIONS = 100
ENCODER = Path('/workspace/build/libjxl_encode')
DECODER = Path('/workspace/build/libjxl_decode')
QUALITIES = list(range(10, 100, 10))
METRIC_LABELS = ['CREATE_TIME', 'SETUP_TIME', 'PROCESS_TIME', 'RESET_TIME', 'DESTROY_TIME']
ENCODING_KEYS = {'quality'}
DECODING_KEYS: set[str] = set()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description='Benchmark libjxl encode/decode workflows over raw RGB24 images.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument('path', help='In opt mode: one raw RGB24 image file. In survey mode: a directory of raw RGB24 images.')
    parser.add_argument('--width', type=int, required=True, help='Image width in pixels.')
    parser.add_argument('--height', type=int, required=True, help='Image height in pixels.')
    parser.add_argument('--mode', choices=['opt', 'survey'], required=True, help='Benchmark mode.')
    parser.add_argument('--output', required=True, help='CSV output pathname.')
    parser.add_argument('--encoding_pars', default='', help='Survey mode only. Comma-separated key:value pairs for encoder parameters.')
    parser.add_argument('--decoding_pars', default='', help='Survey mode only. Must be empty for libjxl because the decoder exposes no benchmark parameters.')
    return parser


def parse_key_value_string(raw: str, valid_keys: set[str], label: str) -> dict[str, str]:
    raw = raw.strip()
    if not raw:
        return {}
    parsed: dict[str, str] = {}
    for item in raw.split(','):
        item = item.strip()
        if not item:
            continue
        if ':' not in item:
            raise ValueError(f"Invalid {label} entry '{item}'. Expected key:value syntax.")
        key, value = item.split(':', 1)
        key = key.strip()
        value = value.strip()
        if key not in valid_keys:
            raise ValueError(f"Unsupported {label} key '{key}'.")
        if not value:
            raise ValueError(f"Missing value for {label} key '{key}'.")
        parsed[key] = value
    return parsed


def validate_dimensions(width: int, height: int) -> None:
    if width <= 0 or height <= 0:
        raise ValueError('--width and --height must be positive integers.')


def validate_raw_image(path: Path, width: int, height: int) -> bytes:
    payload = path.read_bytes()
    expected = width * height * 3
    if len(payload) != expected:
        raise ValueError(f"Input image '{path}' has {len(payload)} bytes, expected {expected} bytes for RGB24.")
    return payload


def parse_metrics(stdout: bytes) -> dict[str, float]:
    text = stdout.decode('utf-8', errors='strict').strip()
    metrics: dict[str, float] = {}
    for line in text.splitlines():
        if ':' not in line:
            continue
        key, value = line.split(':', 1)
        key = key.strip()
        value = value.strip()
        if key in METRIC_LABELS:
            metrics[key] = float(value)
    missing = [k for k in METRIC_LABELS if k not in metrics]
    if missing:
        raise ValueError(f'Missing benchmark metrics: {missing}. Raw stdout: {text!r}')
    return metrics


def run_command(command: list[str], stdin_bytes: bytes | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(command, input=stdin_bytes, capture_output=True, check=True)


def encode_command(width: int, height: int, params: dict[str, str], *, benchmark: bool, output_to_stdout: bool) -> list[str]:
    cmd = [str(ENCODER), '--width', str(width), '--height', str(height)]
    if not output_to_stdout:
        cmd += ['--iterations', str(ITERATIONS)]
    if 'quality' in params:
        cmd += ['--quality', str(params['quality'])]
    if benchmark:
        cmd += ['--benchmark']
    if output_to_stdout:
        cmd += ['--output', '-']
    return cmd


def decode_command(*, benchmark: bool, output_to_stdout: bool) -> list[str]:
    cmd = [str(DECODER)]
    if not output_to_stdout:
        cmd += ['--iterations', str(ITERATIONS)]
    if benchmark:
        cmd += ['--benchmark']
    if output_to_stdout:
        cmd += ['--output', '-']
    return cmd


def throughput(width: int, height: int, metrics: dict[str, float]) -> tuple[float, float]:
    total = metrics['SETUP_TIME'] + metrics['PROCESS_TIME'] + metrics['RESET_TIME']
    if total <= 0.0:
        return float('inf'), float('inf')
    fps = 1.0 / total
    mpx_s = (width * height / 1_000_000.0) / total
    return fps, mpx_s


def image_quality_metrics(original: bytes, decoded: bytes, width: int, height: int) -> tuple[float, float]:
    expected = width * height * 3
    if len(decoded) != expected:
        raise ValueError(f'Decoded payload size mismatch: expected {expected}, got {len(decoded)}.')
    original_np = np.frombuffer(original, dtype=np.uint8).reshape((height, width, 3))
    decoded_np = np.frombuffer(decoded, dtype=np.uint8).reshape((height, width, 3))
    psnr = float(peak_signal_noise_ratio(original_np, decoded_np, data_range=255))
    ssim = float(structural_similarity(original_np, decoded_np, channel_axis=2, data_range=255))
    return psnr, ssim


def generate_opt_configurations() -> list[dict[str, str]]:
    return [{'quality': str(q)} for q in QUALITIES]


def list_survey_images(path: Path) -> list[Path]:
    if not path.is_dir():
        raise ValueError('In survey mode, PATH must be a directory.')
    images = sorted(p for p in path.iterdir() if p.is_file())
    if not images:
        raise ValueError('The survey directory does not contain any files.')
    return images


def progress(current: int, total: int, label: str) -> None:
    width = 28
    filled = int(width * current / max(total, 1))
    bar = '#' * filled + '-' * (width - filled)
    print(f'[{bar}] {current}/{total} {label}', flush=True)


def benchmark_one(image_path: Path, original: bytes, width: int, height: int, enc_params: dict[str, str]) -> dict[str, object]:
    enc_bench_cmd = encode_command(width, height, enc_params, benchmark=True, output_to_stdout=False)
    enc_payload_cmd = encode_command(width, height, enc_params, benchmark=False, output_to_stdout=True)
    dec_bench_cmd = decode_command(benchmark=True, output_to_stdout=False)
    dec_payload_cmd = decode_command(benchmark=False, output_to_stdout=True)

    enc_metrics = parse_metrics(run_command(enc_bench_cmd, stdin_bytes=original).stdout)
    encoded_payload = run_command(enc_payload_cmd, stdin_bytes=original).stdout
    dec_metrics = parse_metrics(run_command(dec_bench_cmd, stdin_bytes=encoded_payload).stdout)
    decoded_payload = run_command(dec_payload_cmd, stdin_bytes=encoded_payload).stdout

    encoded_size = len(encoded_payload)
    raw_size = len(original)
    compression_ratio = (raw_size / encoded_size) if encoded_size else float('inf')
    psnr, ssim = image_quality_metrics(original, decoded_payload, width, height)
    enc_fps, enc_mpx_s = throughput(width, height, enc_metrics)
    dec_fps, dec_mpx_s = throughput(width, height, dec_metrics)

    return {
        'input_filename': image_path.name,
        'quality': enc_params['quality'],
        'encode_create_time_s': enc_metrics['CREATE_TIME'],
        'encode_setup_time_s': enc_metrics['SETUP_TIME'],
        'encode_process_time_s': enc_metrics['PROCESS_TIME'],
        'encode_reset_time_s': enc_metrics['RESET_TIME'],
        'encode_destroy_time_s': enc_metrics['DESTROY_TIME'],
        'decode_create_time_s': dec_metrics['CREATE_TIME'],
        'decode_setup_time_s': dec_metrics['SETUP_TIME'],
        'decode_process_time_s': dec_metrics['PROCESS_TIME'],
        'decode_reset_time_s': dec_metrics['RESET_TIME'],
        'decode_destroy_time_s': dec_metrics['DESTROY_TIME'],
        'encoded_size_bytes': encoded_size,
        'compression_ratio': compression_ratio,
        'psnr_db': psnr,
        'ssim': ssim,
        'encode_fps': enc_fps,
        'encode_mpx_s': enc_mpx_s,
        'decode_fps': dec_fps,
        'decode_mpx_s': dec_mpx_s,
        'encode_command': shlex.join(enc_bench_cmd),
        'decode_command': shlex.join(dec_bench_cmd),
    }


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        validate_dimensions(args.width, args.height)
        path = Path(args.path)
        if not ENCODER.exists():
            raise FileNotFoundError(f'Encoder executable not found: {ENCODER}')
        if not DECODER.exists():
            raise FileNotFoundError(f'Decoder executable not found: {DECODER}')

        rows: list[dict[str, object]] = []
        if args.mode == 'opt':
            if not path.is_file():
                raise ValueError('In opt mode, PATH must be a single raw RGB24 image file.')
            original = validate_raw_image(path, args.width, args.height)
            configs = generate_opt_configurations()
            total = len(configs)
            for idx, enc_params in enumerate(configs, start=1):
                progress(idx, total, f"{path.name} quality={enc_params['quality']}")
                rows.append(benchmark_one(path, original, args.width, args.height, enc_params))
        else:
            enc_params = parse_key_value_string(args.encoding_pars, ENCODING_KEYS, 'encoding_pars')
            dec_params = parse_key_value_string(args.decoding_pars, DECODING_KEYS, 'decoding_pars')
            if dec_params:
                raise ValueError('libjxl decode benchmark has no configurable decoder parameters; --decoding_pars must be empty.')
            if 'quality' not in enc_params:
                raise ValueError("Survey mode requires --encoding_pars with at least 'quality:<value>'.")
            images = list_survey_images(path)
            total = len(images)
            for idx, image_path in enumerate(images, start=1):
                progress(idx, total, image_path.name)
                original = validate_raw_image(image_path, args.width, args.height)
                rows.append(benchmark_one(image_path, original, args.width, args.height, enc_params))

        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        fieldnames = [
            'input_filename', 'quality',
            'encode_create_time_s', 'encode_setup_time_s', 'encode_process_time_s', 'encode_reset_time_s', 'encode_destroy_time_s',
            'decode_create_time_s', 'decode_setup_time_s', 'decode_process_time_s', 'decode_reset_time_s', 'decode_destroy_time_s',
            'encoded_size_bytes', 'compression_ratio', 'psnr_db', 'ssim',
            'encode_fps', 'encode_mpx_s', 'decode_fps', 'decode_mpx_s',
            'encode_command', 'decode_command',
        ]
        with output_path.open('w', newline='', encoding='utf-8') as fh:
            writer = csv.DictWriter(fh, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f'Wrote {len(rows)} rows to {output_path}', flush=True)
        return 0
    except subprocess.CalledProcessError as exc:
        sys.stderr.write('Command failed:\n')
        sys.stderr.write(shlex.join(exc.cmd) + '\n')
        if exc.stderr:
            sys.stderr.write(exc.stderr.decode('utf-8', errors='replace') + '\n')
        return 1
    except Exception as exc:
        sys.stderr.write(f'Error: {exc}\n')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
