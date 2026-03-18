#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import itertools
import math
import os
import re
import subprocess
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import numpy as np

ITERATIONS = 10
QUALITY_VALUES = list(range(10, 100, 10))
SUBSAMPLING_VALUES = ["420", "422", "444"]
DCT_VALUES = ["fast", "float", "int"]
ENTROPY_VALUES = ["huffman", "arithmetic"]
RESTART_VALUES = list(range(0, 33, 8))
METRIC_LABELS = ["CREATE_TIME", "SETUP_TIME", "PROCESS_TIME", "RESET_TIME", "DESTROY_TIME"]

ENCODE_STEM = "jpegli_encode"
DECODE_STEM = "jpegli_decode"


def exe_name(stem: str) -> str:
    return f"{stem}.exe" if os.name == "nt" else stem


def parse_kv_string(text: str | None) -> Dict[str, str]:
    if not text:
        return {}
    items = re.split(r"[\s,]+", text.strip())
    out: Dict[str, str] = {}
    for item in items:
        if not item:
            continue
        if ":" not in item:
            raise ValueError(f"invalid key:value item: {item}")
        key, value = item.split(":", 1)
        out[key.strip()] = value.strip()
    return out


def validate_shared_parameters(enc: Dict[str, str], dec: Dict[str, str]) -> Tuple[Dict[str, str], Dict[str, str]]:
    shared_keys = set(enc).intersection(dec)
    for key in shared_keys:
        if enc[key] != dec[key]:
            raise ValueError(f"shared parameter {key!r} has mismatching values: {enc[key]!r} vs {dec[key]!r}")
    if "dct" in enc and "dct" not in dec:
        dec["dct"] = enc["dct"]
    if "dct" in dec and "dct" not in enc:
        enc["dct"] = dec["dct"]
    if "dct" not in enc and "dct" not in dec:
        enc["dct"] = "int"
        dec["dct"] = "int"
    return enc, dec


def params_to_encode_flags(params: Dict[str, str]) -> List[str]:
    flags: List[str] = []
    for key in ["q", "s", "dct", "entropy", "r"]:
        if key in params:
            flags.extend([f"-{key}", str(params[key])])
    return flags


def params_to_decode_flags(params: Dict[str, str]) -> List[str]:
    flags: List[str] = []
    for key in ["dct"]:
        if key in params:
            flags.extend([f"-{key}", str(params[key])])
    return flags


def parse_metrics(stdout_text: str) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    for line in stdout_text.splitlines():
        line = line.strip()
        if not line or ":" not in line:
            continue
        label, value = line.split(":", 1)
        label = label.strip()
        value = value.strip()
        if label in METRIC_LABELS:
            metrics[label] = float(value)
    missing = [label for label in METRIC_LABELS if label not in metrics]
    if missing:
        raise RuntimeError(f"missing benchmark metrics: {missing}; raw stdout={stdout_text!r}")
    return metrics


def run_binary(binary: Path, args: List[str], stdin_bytes: bytes) -> subprocess.CompletedProcess:
    return subprocess.run([str(binary), *args], input=stdin_bytes, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)


def psnr_rgb(original: bytes, decoded: bytes) -> float:
    a = np.frombuffer(original, dtype=np.uint8).astype(np.float64)
    b = np.frombuffer(decoded, dtype=np.uint8).astype(np.float64)
    mse = np.mean((a - b) ** 2)
    if mse == 0.0:
        return float("inf")
    return 20.0 * math.log10(255.0) - 10.0 * math.log10(mse)


def ssim_rgb(original: bytes, decoded: bytes, width: int, height: int) -> float:
    x = np.frombuffer(original, dtype=np.uint8).astype(np.float64).reshape((height, width, 3))
    y = np.frombuffer(decoded, dtype=np.uint8).astype(np.float64).reshape((height, width, 3))
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    ssim_channels = []
    for ch in range(3):
        xc = x[:, :, ch]
        yc = y[:, :, ch]
        mu_x = float(np.mean(xc))
        mu_y = float(np.mean(yc))
        sigma_x = float(np.var(xc, ddof=0))
        sigma_y = float(np.var(yc, ddof=0))
        sigma_xy = float(np.mean((xc - mu_x) * (yc - mu_y)))
        numerator = (2.0 * mu_x * mu_y + c1) * (2.0 * sigma_xy + c2)
        denominator = (mu_x ** 2 + mu_y ** 2 + c1) * (sigma_x + sigma_y + c2)
        ssim_channels.append(numerator / denominator if denominator != 0.0 else 1.0)
    return float(np.mean(ssim_channels))


def average_iteration_time(metrics: Dict[str, float]) -> float:
    return metrics["SETUP_TIME"] + metrics["PROCESS_TIME"] + metrics["RESET_TIME"]


def throughput(width: int, height: int, avg_seconds: float) -> Tuple[float, float]:
    if avg_seconds <= 0.0:
        return float("inf"), float("inf")
    pixels = float(width * height)
    fps = 1.0 / avg_seconds
    mpix_s = pixels / avg_seconds / 1_000_000.0
    return fps, mpix_s


def load_image_paths(path: Path, mode: str) -> List[Path]:
    if mode == "opt":
        if not path.is_file():
            raise FileNotFoundError(f"opt mode requires a single file: {path}")
        return [path]
    if not path.is_dir():
        raise FileNotFoundError(f"survey mode requires a directory: {path}")
    return sorted([p for p in path.iterdir() if p.is_file()])


def build_opt_parameter_sets() -> Iterable[Tuple[Dict[str, str], Dict[str, str]]]:
    for q, subs, dct, entropy, restart in itertools.product(QUALITY_VALUES, SUBSAMPLING_VALUES, DCT_VALUES, ENTROPY_VALUES, RESTART_VALUES):
        enc = {"q": str(q), "s": subs, "dct": dct, "entropy": entropy, "r": str(restart)}
        dec = {"dct": dct}
        yield enc, dec


def benchmark_pair(image_path: Path, raw_bytes: bytes, width: int, height: int, encode_params: Dict[str, str], decode_params: Dict[str, str], encode_binary: Path, decode_binary: Path) -> Dict[str, object]:
    encode_args_metrics = ["-w", str(width), "-h", str(height), "-i", str(ITERATIONS), *params_to_encode_flags(encode_params), "-b"]
    encode_args_output = ["-w", str(width), "-h", str(height), *params_to_encode_flags(encode_params), "-o", "-"]
    enc_metrics_cp = run_binary(encode_binary, encode_args_metrics, raw_bytes)
    enc_metrics = parse_metrics(enc_metrics_cp.stdout.decode("utf-8", errors="replace"))
    enc_output_cp = run_binary(encode_binary, encode_args_output, raw_bytes)
    encoded_bytes = bytes(enc_output_cp.stdout)

    decode_args_metrics = ["-w", str(width), "-h", str(height), "-i", str(ITERATIONS), *params_to_decode_flags(decode_params), "-b"]
    decode_args_output = ["-w", str(width), "-h", str(height), *params_to_decode_flags(decode_params), "-o", "-"]
    dec_metrics_cp = run_binary(decode_binary, decode_args_metrics, encoded_bytes)
    dec_metrics = parse_metrics(dec_metrics_cp.stdout.decode("utf-8", errors="replace"))
    dec_output_cp = run_binary(decode_binary, decode_args_output, encoded_bytes)
    decoded_bytes = bytes(dec_output_cp.stdout)

    encoded_size = len(encoded_bytes)
    compression_ratio = (len(raw_bytes) / encoded_size) if encoded_size else float("inf")
    psnr = psnr_rgb(raw_bytes, decoded_bytes)
    ssim = ssim_rgb(raw_bytes, decoded_bytes, width, height)
    enc_avg = average_iteration_time(enc_metrics)
    dec_avg = average_iteration_time(dec_metrics)
    enc_fps, enc_mpix = throughput(width, height, enc_avg)
    dec_fps, dec_mpix = throughput(width, height, dec_avg)

    row = {
        "input_filename": image_path.name,
        "encoded_size_bytes": encoded_size,
        "compression_ratio": compression_ratio,
        "psnr_db": psnr,
        "ssim": ssim,
        "encode_fps": enc_fps,
        "encode_mpix_s": enc_mpix,
        "decode_fps": dec_fps,
        "decode_mpix_s": dec_mpix,
        "encode_command": " ".join([str(encode_binary), *encode_args_metrics]),
        "decode_command": " ".join([str(decode_binary), *decode_args_metrics]),
    }
    for label in METRIC_LABELS:
        row[f"encode_{label.lower()}"] = enc_metrics[label]
        row[f"decode_{label.lower()}"] = dec_metrics[label]
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark encode/decode executables")
    parser.add_argument("path", help="single raw RGB24 image for opt mode, or directory of raw RGB24 images for survey mode")
    parser.add_argument("-w", type=int, required=True, help="image width")
    parser.add_argument("-h", type=int, required=True, help="image height")
    parser.add_argument("-mode", choices=["opt", "survey"], required=True, help="benchmark mode")
    parser.add_argument("-o", required=True, help="output CSV path")
    parser.add_argument("-encoding_pars", default=None, help="survey mode encode parameters as key:value items")
    parser.add_argument("-decoding_pars", default=None, help="survey mode decode parameters as key:value items")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    encode_binary = script_dir / exe_name(ENCODE_STEM)
    decode_binary = script_dir / exe_name(DECODE_STEM)
    if not encode_binary.exists():
        raise FileNotFoundError(f"encode binary not found: {encode_binary}")
    if not decode_binary.exists():
        raise FileNotFoundError(f"decode binary not found: {decode_binary}")

    image_paths = load_image_paths(Path(args.path), args.mode)
    expected_raw_size = args.w * args.h * 3

    if args.mode == "survey":
        enc_fixed = parse_kv_string(args.encoding_pars)
        dec_fixed = parse_kv_string(args.decoding_pars)
        enc_fixed, dec_fixed = validate_shared_parameters(enc_fixed, dec_fixed)
        param_sets = [(enc_fixed, dec_fixed)]
    else:
        param_sets = list(build_opt_parameter_sets())

    rows: List[Dict[str, object]] = []
    for image_path in image_paths:
        raw_bytes = image_path.read_bytes()
        if len(raw_bytes) != expected_raw_size:
            raise ValueError(f"raw RGB24 file size mismatch for {image_path}: got {len(raw_bytes)}, expected {expected_raw_size}")
        for enc_params, dec_params in param_sets:
            enc_params = dict(enc_params)
            dec_params = dict(dec_params)
            enc_params, dec_params = validate_shared_parameters(enc_params, dec_params)
            rows.append(benchmark_pair(image_path, raw_bytes, args.w, args.h, enc_params, dec_params, encode_binary, decode_binary))

    output_path = Path(args.o)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "input_filename",
        *[f"encode_{label.lower()}" for label in METRIC_LABELS],
        *[f"decode_{label.lower()}" for label in METRIC_LABELS],
        "encoded_size_bytes",
        "compression_ratio",
        "psnr_db",
        "ssim",
        "encode_fps",
        "encode_mpix_s",
        "decode_fps",
        "decode_mpix_s",
        "encode_command",
        "decode_command",
    ]
    with output_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
