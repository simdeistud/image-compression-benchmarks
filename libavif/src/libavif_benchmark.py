#!/usr/bin/env python3
import argparse
import csv
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import numpy as np
from skimage.metrics import peak_signal_noise_ratio, structural_similarity

ITERATIONS = 10
ENCODE_BIN = "/workspace/build/libavif_encode"
DECODE_BIN = "/workspace/build/libavif_decode"
ENCODE_BACKENDS = ["aom", "rav1e", "svt"]
DECODE_BACKENDS = ["aom", "dav1d", "libgav1"]
SUBSAMPLINGS = ["444", "422", "420"]
QUALITIES = list(range(10, 100, 10))


@dataclass
class RunResult:
    metrics: Dict[str, float]
    payload: bytes
    command: str


def parse_kv_string(value: str) -> Dict[str, str]:
    if not value:
        return {}
    out: Dict[str, str] = {}
    for item in value.split(","):
        item = item.strip()
        if not item:
            continue
        if ":" not in item:
            raise ValueError(f"Invalid key:value pair: {item}")
        k, v = item.split(":", 1)
        out[k.strip()] = v.strip()
    return out


def help_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="libavif_benchmark.py",
        description=(
            "Benchmark libavif encode/decode on raw RGB24 images. In opt mode PATH is a single image; "
            "in survey mode PATH is a directory."
        ),
    )
    p.add_argument("path", help="Path to a raw RGB24 image (opt) or a directory containing raw RGB24 images (survey)")
    p.add_argument("--width", type=int, required=True, help="Image width in pixels")
    p.add_argument("--height", type=int, required=True, help="Image height in pixels")
    p.add_argument("--mode", choices=["opt", "survey"], required=True, help="Benchmark mode")
    p.add_argument("--output", required=True, help="CSV output path")
    p.add_argument(
        "--encoding_pars",
        default="",
        help="Survey mode only. Comma-separated key:value list, e.g. backend:aom,quality:50,subsampling:444",
    )
    p.add_argument(
        "--decoding_pars",
        default="",
        help="Survey mode only. Comma-separated key:value list, e.g. backend:dav1d",
    )
    return p


def read_raw_rgb24(path: Path, width: int, height: int) -> bytes:
    data = path.read_bytes()
    expected = width * height * 3
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes for RGB24 {width}x{height}, got {len(data)}")
    return data


def parse_metrics(stdout: bytes) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    for line in stdout.decode("utf-8", errors="replace").splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()
        if key in {"CREATE_TIME", "SETUP_TIME", "PROCESS_TIME", "RESET_TIME", "DESTROY_TIME"}:
            metrics[key] = float(value)
    required = {"CREATE_TIME", "SETUP_TIME", "PROCESS_TIME", "RESET_TIME", "DESTROY_TIME"}
    missing = required - metrics.keys()
    if missing:
        raise RuntimeError(f"Missing benchmark metrics: {sorted(missing)}")
    return metrics


def run_checked(command: List[str], stdin_bytes: bytes) -> subprocess.CompletedProcess:
    proc = subprocess.run(
        command,
        input=stdin_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"Command failed ({proc.returncode}): {' '.join(map(shlex.quote, command))}\n"
            f"stderr:\n{proc.stderr.decode('utf-8', errors='replace')}"
        )
    return proc


def encode_bench_cmd(width: int, height: int, backend: str, quality: int, subsampling: str) -> List[str]:
    return [
        ENCODE_BIN,
        "--width",
        str(width),
        "--height",
        str(height),
        "--iterations",
        str(ITERATIONS),
        "--quality",
        str(quality),
        "--subsampling",
        subsampling,
        "--backend",
        backend,
        "--benchmark",
    ]


def encode_out_cmd(width: int, height: int, backend: str, quality: int, subsampling: str) -> List[str]:
    return [
        ENCODE_BIN,
        "--width",
        str(width),
        "--height",
        str(height),
        "--quality",
        str(quality),
        "--subsampling",
        subsampling,
        "--backend",
        backend,
        "--output",
        "-",
    ]


def decode_bench_cmd(backend: str) -> List[str]:
    return [
        DECODE_BIN,
        "--iterations",
        str(ITERATIONS),
        "--backend",
        backend,
        "--benchmark",
    ]


def decode_out_cmd(backend: str) -> List[str]:
    return [
        DECODE_BIN,
        "--backend",
        backend,
        "--output",
        "-",
    ]


def benchmark_and_capture(bench_cmd: List[str], out_cmd: List[str], stdin_bytes: bytes, display_cmd: str) -> RunResult:
    bench_proc = run_checked(bench_cmd, stdin_bytes)
    metrics = parse_metrics(bench_proc.stdout)
    out_proc = run_checked(out_cmd, stdin_bytes)
    return RunResult(metrics=metrics, payload=out_proc.stdout, command=display_cmd)


def psnr_ssim(raw_rgb: bytes, decoded_rgb: bytes, width: int, height: int) -> Tuple[float, float]:
    a = np.frombuffer(raw_rgb, dtype=np.uint8).reshape((height, width, 3))
    b = np.frombuffer(decoded_rgb, dtype=np.uint8).reshape((height, width, 3))
    psnr = float(peak_signal_noise_ratio(a, b, data_range=255))
    ssim = float(structural_similarity(a, b, channel_axis=2, data_range=255))
    return psnr, ssim


def throughput(width: int, height: int, metrics: Dict[str, float]) -> Tuple[float, float]:
    total = metrics["SETUP_TIME"] + metrics["PROCESS_TIME"] + metrics["RESET_TIME"]
    fps = 0.0 if total <= 0 else 1.0 / total
    mpx = 0.0 if total <= 0 else (width * height / 1_000_000.0) / total
    return fps, mpx


def configuration_grid() -> Iterable[Tuple[Dict[str, str], Dict[str, str]]]:
    for backend in ENCODE_BACKENDS:
        for quality in QUALITIES:
            for subsampling in SUBSAMPLINGS:
                enc = {"backend": backend, "quality": str(quality), "subsampling": subsampling}
                dec_backend = backend if backend == "aom" else "aom"
                dec = {"backend": dec_backend}
                yield enc, dec
    for backend in [b for b in DECODE_BACKENDS if b != "aom"]:
        for quality in QUALITIES:
            for subsampling in SUBSAMPLINGS:
                enc = {"backend": "aom", "quality": str(quality), "subsampling": subsampling}
                dec = {"backend": backend}
                yield enc, dec


def validate_combo(width: int, height: int, raw_rgb: bytes, enc: Dict[str, str], dec: Dict[str, str]) -> bool:
    try:
        enc_proc = run_checked(
            encode_out_cmd(width, height, enc["backend"], int(enc["quality"]), enc["subsampling"]),
            raw_rgb,
        )
        run_checked(decode_out_cmd(dec["backend"]), enc_proc.stdout)
        return True
    except Exception:
        return False


def image_paths(path: Path, mode: str) -> List[Path]:
    if mode == "opt":
        if not path.is_file():
            raise ValueError("In opt mode PATH must be a single file")
        return [path]
    if not path.is_dir():
        raise ValueError("In survey mode PATH must be a directory")
    return sorted([p for p in path.iterdir() if p.is_file()])


def run_one(raw_path: Path, width: int, height: int, enc: Dict[str, str], dec: Dict[str, str]) -> Dict[str, object]:
    raw_rgb = read_raw_rgb24(raw_path, width, height)

    enc_run = benchmark_and_capture(
        encode_bench_cmd(width, height, enc["backend"], int(enc["quality"]), enc["subsampling"]),
        encode_out_cmd(width, height, enc["backend"], int(enc["quality"]), enc["subsampling"]),
        raw_rgb,
        " ".join(map(shlex.quote, encode_bench_cmd(width, height, enc['backend'], int(enc['quality']), enc['subsampling'])[:-1])),
    )
    encoded = enc_run.payload
    encoded_size = len(encoded)
    compression_ratio = 0.0 if encoded_size == 0 else (len(raw_rgb) / encoded_size)

    dec_run = benchmark_and_capture(
        decode_bench_cmd(dec["backend"]),
        decode_out_cmd(dec["backend"]),
        encoded,
        " ".join(map(shlex.quote, decode_bench_cmd(dec['backend'])[:-1])),
    )
    decoded = dec_run.payload

    psnr, ssim = psnr_ssim(raw_rgb, decoded, width, height)
    enc_fps, enc_mpx = throughput(width, height, enc_run.metrics)
    dec_fps, dec_mpx = throughput(width, height, dec_run.metrics)

    row: Dict[str, object] = {
        "filename": raw_path.name,
        "encoded_size_bytes": encoded_size,
        "compression_ratio": compression_ratio,
        "psnr": psnr,
        "ssim": ssim,
        "encoding_command": enc_run.command,
        "decoding_command": dec_run.command,
        "encoding_backend": enc["backend"],
        "encoding_quality": enc["quality"],
        "encoding_subsampling": enc["subsampling"],
        "decoding_backend": dec["backend"],
        "encode_create_time_s": enc_run.metrics["CREATE_TIME"],
        "encode_setup_time_s": enc_run.metrics["SETUP_TIME"],
        "encode_process_time_s": enc_run.metrics["PROCESS_TIME"],
        "encode_reset_time_s": enc_run.metrics["RESET_TIME"],
        "encode_destroy_time_s": enc_run.metrics["DESTROY_TIME"],
        "decode_create_time_s": dec_run.metrics["CREATE_TIME"],
        "decode_setup_time_s": dec_run.metrics["SETUP_TIME"],
        "decode_process_time_s": dec_run.metrics["PROCESS_TIME"],
        "decode_reset_time_s": dec_run.metrics["RESET_TIME"],
        "decode_destroy_time_s": dec_run.metrics["DESTROY_TIME"],
        "encode_fps": enc_fps,
        "encode_mpx_per_s": enc_mpx,
        "decode_fps": dec_fps,
        "decode_mpx_per_s": dec_mpx,
    }
    return row


def main() -> int:
    parser = help_parser()
    args = parser.parse_args()

    path = Path(args.path)
    width = args.width
    height = args.height
    output_csv = Path(args.output)

    rows: List[Dict[str, object]] = []
    imgs = image_paths(path, args.mode)

    if args.mode == "opt":
        raw_rgb = read_raw_rgb24(imgs[0], width, height)
        combos = []
        for enc, dec in configuration_grid():
            if validate_combo(width, height, raw_rgb, enc, dec):
                combos.append((enc, dec))
        total = len(combos)
        for idx, (enc, dec) in enumerate(combos, start=1):
            print(f"[{idx}/{total}] {imgs[0].name} enc={enc} dec={dec}", flush=True)
            rows.append(run_one(imgs[0], width, height, enc, dec))
    else:
        enc = parse_kv_string(args.encoding_pars)
        dec = parse_kv_string(args.decoding_pars)
        if set(enc.keys()) != {"backend", "quality", "subsampling"}:
            parser.error("--encoding_pars must define exactly backend,quality,subsampling in survey mode")
        if set(dec.keys()) != {"backend"}:
            parser.error("--decoding_pars must define exactly backend in survey mode")
        total = len(imgs)
        for idx, img in enumerate(imgs, start=1):
            print(f"[{idx}/{total}] {img.name} enc={enc} dec={dec}", flush=True)
            rows.append(run_one(img, width, height, enc, dec))

    if not rows:
        raise RuntimeError("No benchmark results were produced")

    output_csv.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0].keys())
    with output_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Saved {len(rows)} rows to {output_csv}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(2)
