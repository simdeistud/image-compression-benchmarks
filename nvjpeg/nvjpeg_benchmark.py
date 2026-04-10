#!/usr/bin/env python3
import sys
import re
from pathlib import Path
import subprocess
import numpy as np
from skimage.metrics import peak_signal_noise_ratio, structural_similarity

# Paths (relative to where you run the script)
ENCODE_PROG_PATH = "build/nvjpeg_encode"
DECODE_PROG_PATH = "build/nvjpeg_decode"

SUBSAMPLINGS = ["444", "422", "420"]
QUALITIES = range(10, 101, 10) # nvjpeg doesn't support quality=0

def run_cmd(cmd, stdin_bytes: bytes):
    """
    Run cmd with binary stdin/stdout/stderr capture.
    stdout is returned as bytes (binary payload).
    stderr is returned as decoded text (benchmark/stat lines).
    """
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=False
    )
    out, err = proc.communicate(input=stdin_bytes)
    return out, err.decode("utf-8", errors="replace"), proc.returncode


def extract_total_seconds(stderr_text: str) -> float:
    """
    Prefer parsing the explicit 'Total processing time (seconds):' line.
    Fallback to first float if format differs.
    """
    m = re.search(r"Total processing time \(seconds\):\s*([0-9.+-eE]+)", stderr_text)
    if m:
        return float(m.group(1))

    # fallback: first float anywhere
    m = re.search(r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?", stderr_text)
    if not m:
        raise ValueError(f"Cannot parse time from stderr: {stderr_text!r}")
    return float(m.group(0))


def rgb24_bytes_to_image(buf: bytes, width: int, height: int) -> np.ndarray:
    arr = np.frombuffer(buf, dtype=np.uint8)
    expected = width * height * 3
    if arr.size != expected:
        raise ValueError(f"RGB24 size mismatch: got {arr.size}, expected {expected}")
    return arr.reshape((height, width, 3))


def compute_psnr(orig_rgb: np.ndarray, dec_rgb: np.ndarray) -> float:
    return peak_signal_noise_ratio(orig_rgb, dec_rgb, data_range=255)


def compute_ssim(orig_rgb: np.ndarray, dec_rgb: np.ndarray) -> float:
    return structural_similarity(orig_rgb, dec_rgb, data_range=255, channel_axis=2)


def main():
    # Args: widthxheight, rgb_image_folder_path, results_folder_path, iterations(optional)
    if len(sys.argv) < 4:
        print("Usage: python3 benchmark.py WIDTHxHEIGHT IMAGE_FOLDER RESULTS_FOLDER [ITERATIONS]", file=sys.stderr)
        sys.exit(1)

    resolution = sys.argv[1]
    width, height = map(int, resolution.split("x"))
    image_folder = Path(sys.argv[2])
    results_folder = Path(sys.argv[3])
    iterations = int(sys.argv[4]) if len(sys.argv) > 4 else 100

    results_folder.mkdir(parents=True, exist_ok=True)

    enc_path = Path(ENCODE_PROG_PATH).resolve()
    dec_path = Path(DECODE_PROG_PATH).resolve()
    if not enc_path.exists():
        print(f"ERROR: encoder not found at: {enc_path}", file=sys.stderr)
        sys.exit(1)
    if not dec_path.exists():
        print(f"ERROR: decoder not found at: {dec_path}", file=sys.stderr)
        sys.exit(1)

    rgb_images = [p for p in image_folder.iterdir() if p.is_file()]
    if not rgb_images:
        print(f"ERROR: no files found in {image_folder}", file=sys.stderr)
        sys.exit(1)

    combinations = (len(rgb_images) * len(SUBSAMPLINGS) * len(QUALITIES))
    current = 0
    print(f"Total combinations to test: {combinations}")

    out_csv = results_folder / f"results_{resolution}.csv"
    if not out_csv.exists() or out_csv.stat().st_size == 0:
        out_csv.write_text(
            "img_name,subsampling,quality,"
            "enc_avg_time_milliseconds,enc_avg_fps,enc_avg_mps,"
            "dec_avg_time_milliseconds,dec_avg_fps,dec_avg_mps,"
            "compressed_size_bytes,compression_ratio,bpp,psnr,ssim\n"
        )

    expected_bytes = width * height * 3

    with out_csv.open("a", buffering=1) as f:
        for img_path in rgb_images:
            img_name = img_path.name
            img_bytes = img_path.read_bytes()

            if len(img_bytes) != expected_bytes:
                print(f"[SKIP] {img_name}: size {len(img_bytes)} != expected {expected_bytes} (RGB24 {width}x{height})",
                      file=sys.stderr)
                continue

            orig_img = rgb24_bytes_to_image(img_bytes, width, height)

            for subsampling in SUBSAMPLINGS:
                for quality in QUALITIES:
                    current += 1
                    print(f"Testing combination {current}/{combinations}")

                    # --- ENCODE ---
                    enc_cmd = [
                        str(enc_path),
                        "--width", str(width),
                        "--height", str(height),
                        "--subsampling", subsampling,
                        "--quality", str(quality),
                        "--iterations", str(iterations),
                        "--benchmark",
                        "--input", "-",
                        "--output", "-"
                    ]

                    enc_out, enc_err, enc_rc = run_cmd(enc_cmd, img_bytes)
                    if enc_rc != 0:
                        # unsupported feature combos end here (e.g., arithmetic not compiled)
                        print(f"[ENC ERROR] {img_name} rc={enc_rc} stderr={enc_err[:200]!r}", file=sys.stderr)
                        continue

                    encoded_jpeg = enc_out
                    if len(encoded_jpeg) == 0:
                        print(f"[ENC ERROR] {img_name} produced EMPTY stdout (0 bytes)", file=sys.stderr)
                        print(f"  enc_cmd={enc_cmd}", file=sys.stderr)
                        print(f"  enc_stderr_head={enc_err[:400]!r}", file=sys.stderr)
                        continue

                    try:
                        enc_total_time_s = extract_total_seconds(enc_err)
                    except Exception as e:
                        print(f"[ENC PARSE ERROR] {img_name}: {e} stderr={enc_err[:400]!r}", file=sys.stderr)
                        continue

                    enc_avg_ms = (enc_total_time_s / iterations) * 1000.0
                    enc_avg_fps = (1000.0 / enc_avg_ms) if enc_avg_ms > 0 else 0.0
                    enc_avg_mps = (width * height) / (enc_avg_ms * 1e3) if enc_avg_ms > 0 else 0.0

                    compressed_size = len(encoded_jpeg)
                    compression_ratio = (width * height * 3) / compressed_size if compressed_size > 0 else 0.0
                    bpp = (compressed_size * 8) / (width * height) if compressed_size > 0 else 0.0

                    # --- DECODE ---
                    dec_cmd = [
                        str(dec_path),
                        "--iterations", str(iterations),
                        "--benchmark",
                        "--input", "-",
                        "--output", "-"
                    ]

                    dec_out, dec_err, dec_rc = run_cmd(dec_cmd, encoded_jpeg)
                    if dec_rc != 0:
                        print(f"[DEC ERROR] {img_name} rc={dec_rc} stderr={dec_err[:200]!r}", file=sys.stderr)
                        continue

                    decoded_rgb_bytes = dec_out
                    if len(decoded_rgb_bytes) == 0:
                        print(f"[DEC ERROR] {img_name} produced EMPTY RGB stdout (0 bytes)", file=sys.stderr)
                        print(f"  dec_cmd={dec_cmd}", file=sys.stderr)
                        print(f"  dec_stderr_head={dec_err[:400]!r}", file=sys.stderr)
                        continue

                    if len(decoded_rgb_bytes) != expected_bytes:
                        print(f"[DEC ERROR] {img_name}: decoded size {len(decoded_rgb_bytes)} != expected {expected_bytes}",
                              file=sys.stderr)
                        continue

                    try:
                        dec_total_time_s = extract_total_seconds(dec_err)
                    except Exception as e:
                        print(f"[DEC PARSE ERROR] {img_name}: {e} stderr={dec_err[:400]!r}", file=sys.stderr)
                        continue

                    dec_avg_ms = (dec_total_time_s / iterations) * 1000.0
                    dec_avg_fps = (1000.0 / dec_avg_ms) if dec_avg_ms > 0 else 0.0
                    dec_avg_mps = (width * height) / (dec_avg_ms * 1e3) if dec_avg_ms > 0 else 0.0

                    dec_img = rgb24_bytes_to_image(decoded_rgb_bytes, width, height)
                    psnr = compute_psnr(orig_img, dec_img)
                    ssim = compute_ssim(orig_img, dec_img)

                    f.write(
                        f"{img_name},{subsampling},{quality},"
                        f"{enc_avg_ms:.3f},{enc_avg_fps:.3f},{enc_avg_mps:.6f},"
                        f"{dec_avg_ms:.3f},{dec_avg_fps:.3f},{dec_avg_mps:.6f},"
                        f"{compressed_size},{compression_ratio:.6f},{bpp:.6f},"
                        f"{psnr:.4f},{ssim:.6f}\n"
                    )


if __name__ == "__main__":
    main()