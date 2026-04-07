#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
import os
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import numpy as np
import pandas as pd
from skimage.metrics import peak_signal_noise_ratio, structural_similarity

ITERATIONS = 10
ENCODE_QUALITY_VALUES = list(range(10, 100, 10))
METRIC_LABELS = [
    "CREATE_TIME",
    "SETUP_TIME",
    "PROCESS_TIME",
    "RESET_TIME",
    "DESTROY_TIME",
]


def parse_kv_string(value: str | None) -> Dict[str, str]:
    if value is None:
        return {}
    value = value.strip()
    if not value:
        return {}
    result: Dict[str, str] = {}
    for item in value.split(','):
        item = item.strip()
        if not item:
            continue
        if ':' not in item:
            raise argparse.ArgumentTypeError(
                f"Invalid key:value item '{item}'. Use comma-separated key:value pairs."
            )
        key, raw = item.split(':', 1)
        key = key.strip()
        raw = raw.strip()
        if not key:
            raise argparse.ArgumentTypeError(f"Invalid empty key in '{item}'.")
        result[key] = raw
    return result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="libwebp_benchmark.py",
        description=(
            "Benchmark libwebp encode/decode executables on raw RGB24 images. "
            "In opt mode PATH is a single raw RGB24 file. In survey mode PATH is a directory of raw RGB24 files."
        ),
    )
    parser.add_argument("path", help="Input file (opt mode) or directory (survey mode).")
    parser.add_argument("--width", type=int, required=True, help="Image width in pixels.")
    parser.add_argument("--height", type=int, required=True, help="Image height in pixels.")
    parser.add_argument(
        "--mode",
        choices=["opt", "survey"],
        required=True,
        help="opt = sweep valid parameter combinations on one image; survey = fixed parameters on a folder.",
    )
    parser.add_argument("--output", required=True, help="Output CSV path.")
    parser.add_argument(
        "--encoding_pars",
        default="",
        help="Survey mode only. Comma-separated key:value list for encoder parameters, e.g. quality:80.",
    )
    parser.add_argument(
        "--decoding_pars",
        default="",
        help="Survey mode only. Comma-separated key:value list for decoder parameters.",
    )
    return parser


def script_root() -> Path:
    return Path(__file__).resolve().parent


def build_root() -> Path:
    return script_root().parent / "build"


def encode_executable() -> Path:
    return build_root() / "libwebp_encode"


def decode_executable() -> Path:
    return build_root() / "libwebp_decode"


def read_raw_rgb24(path: Path, width: int, height: int) -> bytes:
    data = path.read_bytes()
    expected = width * height * 3
    if len(data) != expected:
        raise ValueError(
            f"{path}: expected {expected} bytes for raw RGB24 ({width}x{height}), found {len(data)} bytes"
        )
    return data


def parse_metrics(stdout: bytes) -> Dict[str, float]:
    text = stdout.decode("utf-8", errors="strict")
    result: Dict[str, float] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if ':' not in line:
            raise ValueError(f"Malformed metric line: {line!r}")
        key, value = line.split(':', 1)
        result[key.strip()] = float(value.strip())
    missing = [k for k in METRIC_LABELS if k not in result]
    if missing:
        raise ValueError(f"Missing metrics: {missing}")
    return result


def run_command(cmd: List[str], stdin_payload: bytes) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd,
        input=stdin_payload,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )


def make_encode_cmd(width: int, height: int, params: Dict[str, str], benchmark: bool, output_mode: str | None) -> List[str]:
    cmd = [str(encode_executable()), "--width", str(width), "--height", str(height)]
    if benchmark:
        cmd += ["--iterations", str(ITERATIONS)]
    if "quality" in params:
        cmd += ["--quality", str(params["quality"])]
    if benchmark:
        cmd.append("--benchmark")
    elif output_mode is not None:
        cmd += ["--output", output_mode]
    return cmd


def make_decode_cmd(params: Dict[str, str], benchmark: bool, output_mode: str | None) -> List[str]:
    cmd = [str(decode_executable())]
    if benchmark:
        cmd += ["--iterations", str(ITERATIONS)]
    # libwebp_decode currently exposes no extra codec parameters.
    if benchmark:
        cmd.append("--benchmark")
    elif output_mode is not None:
        cmd += ["--output", output_mode]
    return cmd


def throughput_from_metrics(metrics: Dict[str, float], width: int, height: int) -> Tuple[float, float]:
    avg_iter = metrics["SETUP_TIME"] + metrics["PROCESS_TIME"] + metrics["RESET_TIME"]
    if avg_iter <= 0.0:
        return math.inf, math.inf
    fps = 1.0 / avg_iter
    mpx_s = (width * height / 1_000_000.0) / avg_iter
    return fps, mpx_s


def psnr_ssim(raw_rgb: bytes, decoded_rgb: bytes, width: int, height: int) -> Tuple[float, float]:
    ref = np.frombuffer(raw_rgb, dtype=np.uint8).reshape((height, width, 3))
    dec = np.frombuffer(decoded_rgb, dtype=np.uint8).reshape((height, width, 3))
    psnr = float(peak_signal_noise_ratio(ref, dec, data_range=255))
    ssim = float(structural_similarity(ref, dec, data_range=255, channel_axis=2))
    return psnr, ssim


def emit_progress(current: int, total: int, message: str) -> None:
    print(f"[{current}/{total}] {message}", flush=True)


def opt_configurations() -> List[Tuple[Dict[str, str], Dict[str, str]]]:
    return [({"quality": str(q)}, {}) for q in ENCODE_QUALITY_VALUES]


def survey_configurations(encode_params: Dict[str, str], decode_params: Dict[str, str]) -> List[Tuple[Dict[str, str], Dict[str, str]]]:
    return [(dict(encode_params), dict(decode_params))]


def collect_images(path: Path, mode: str) -> List[Path]:
    if mode == "opt":
        if not path.is_file():
            raise ValueError("In opt mode PATH must be a single file.")
        return [path]
    if not path.is_dir():
        raise ValueError("In survey mode PATH must be a directory.")
    return sorted([p for p in path.iterdir() if p.is_file()])


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.width <= 0 or args.height <= 0:
        parser.error("--width and --height must be > 0")

    enc_path = encode_executable()
    dec_path = decode_executable()
    if not enc_path.exists() or not dec_path.exists():
        parser.error(
            f"Executables not found. Expected '{enc_path}' and '{dec_path}'. Build the CMake project first."
        )

    try:
        enc_survey = parse_kv_string(args.encoding_pars)
        dec_survey = parse_kv_string(args.decoding_pars)
    except argparse.ArgumentTypeError as exc:
        parser.error(str(exc))

    path = Path(args.path)
    images = collect_images(path, args.mode)
    if not images:
        parser.error("No input images found.")

    if args.mode == "opt":
        configs = opt_configurations()
    else:
        configs = survey_configurations(enc_survey, dec_survey)

    total = len(images) * len(configs)
    rows: List[Dict[str, object]] = []
    step = 0

    for image_path in images:
        raw_rgb = read_raw_rgb24(image_path, args.width, args.height)
        for enc_params, dec_params in configs:
            step += 1
            human_cfg = f"{image_path.name} | encode={enc_params or '{}'} | decode={dec_params or '{}'}"
            emit_progress(step, total, human_cfg)

            enc_bench_cmd = make_encode_cmd(args.width, args.height, enc_params, benchmark=True, output_mode=None)
            enc_bench_cp = run_command(enc_bench_cmd, raw_rgb)
            enc_metrics = parse_metrics(enc_bench_cp.stdout)

            enc_out_cmd = make_encode_cmd(args.width, args.height, enc_params, benchmark=False, output_mode='-')
            enc_out_cp = run_command(enc_out_cmd, raw_rgb)
            encoded_payload = enc_out_cp.stdout

            dec_bench_cmd = make_decode_cmd(dec_params, benchmark=True, output_mode=None)
            dec_bench_cp = run_command(dec_bench_cmd, encoded_payload)
            dec_metrics = parse_metrics(dec_bench_cp.stdout)

            dec_out_cmd = make_decode_cmd(dec_params, benchmark=False, output_mode='-')
            dec_out_cp = run_command(dec_out_cmd, encoded_payload)
            decoded_payload = dec_out_cp.stdout

            expected_decoded = args.width * args.height * 3
            if len(decoded_payload) != expected_decoded:
                raise ValueError(
                    f"Decoded payload size mismatch for {image_path.name}: expected {expected_decoded}, got {len(decoded_payload)}"
                )

            encoded_size = len(encoded_payload)
            compression_ratio = (len(raw_rgb) / encoded_size) if encoded_size > 0 else math.inf
            psnr, ssim = psnr_ssim(raw_rgb, decoded_payload, args.width, args.height)
            enc_fps, enc_mpx_s = throughput_from_metrics(enc_metrics, args.width, args.height)
            dec_fps, dec_mpx_s = throughput_from_metrics(dec_metrics, args.width, args.height)

            row: Dict[str, object] = {
                "filename": image_path.name,
                "width": args.width,
                "height": args.height,
                "raw_size_bytes": len(raw_rgb),
                "encoded_size_bytes": encoded_size,
                "compression_ratio": compression_ratio,
                "psnr": psnr,
                "ssim": ssim,
                "encode_fps": enc_fps,
                "encode_mpx_s": enc_mpx_s,
                "decode_fps": dec_fps,
                "decode_mpx_s": dec_mpx_s,
                "encode_command_benchmark": shlex.join(enc_bench_cmd),
                "encode_command_output": shlex.join(enc_out_cmd),
                "decode_command_benchmark": shlex.join(dec_bench_cmd),
                "decode_command_output": shlex.join(dec_out_cmd),
            }

            for label in METRIC_LABELS:
                row[f"encode_{label.lower()}"] = enc_metrics[label]
                row[f"decode_{label.lower()}"] = dec_metrics[label]

            if enc_params:
                for key, value in enc_params.items():
                    row[f"encode_{key}"] = value
            if dec_params:
                for key, value in dec_params.items():
                    row[f"decode_{key}"] = value

            rows.append(row)

    df = pd.DataFrame(rows)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(output_path, index=False, quoting=csv.QUOTE_MINIMAL)
    print(f"Completed {len(rows)} benchmark row(s). CSV written to {output_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        sys.stderr.write("Command failed:\n")
        sys.stderr.write(f"  {' '.join(shlex.quote(x) for x in exc.cmd)}\n")
        if exc.stderr:
            sys.stderr.write(exc.stderr.decode('utf-8', errors='replace'))
        raise
