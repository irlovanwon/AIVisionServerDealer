#!/usr/bin/env python3
"""
CPU Performance Test for AIVisionServerDealer

Tests the full pipeline (Data Server PUB -> AIVisionServerDealer -> AI Server -> Result SUB)
under both GPU and CPU YOLO inference modes, across all 3 transport modes (Binary/File/Http).

Requirements:
  - AI server (ecid_2026_r1) running on the same host
  - AIVisionServerDealer running with production config (IPC endpoints)
  - Test images at IMAGES_DIR

Usage:
  python3 tests/test_cpu.py [--images N] [--gpu-only] [--cpu-only] [--no-gpu-compare]

Output:
  Per-image latency, throughput, detection counts, and a GPU-vs-CPU comparison report.
"""

import zmq
import json
import time
import sys
import os
import glob
import argparse
import threading
import subprocess
import urllib3
import requests

urllib3.disable_warnings()

# ==================== Configuration ====================
AI_SERVER_CONFIG_URL = "http://127.0.0.1:8000/ops/config"
ADMIN_URL = "https://127.0.0.1:8445/admin"
IMAGE_PUB_ENDPOINT = "ipc:///tmp/sc_pub_stereo_image"
RESULT_SUB_ENDPOINT = "ipc:///tmp/ai_vision_dealer_result"
IMAGES_DIR = "/home/user/ECIDS/AITestImages/Task1"
RESULT_TIMEOUT_S = 120  # per-image timeout (CPU YOLO can be slow)
WARMUP_IMAGES = 1       # warmup images before measurement

TRANSPORT_MODES = ["Binary", "File", "Http"]


# ==================== Helpers ====================
def set_ai_engine_mode(mode):
    """Switch AI server YOLO engine between GPU and CPU."""
    r = requests.post(AI_SERVER_CONFIG_URL, json={
        "Action": "SetParameter",
        "Parameter": [{"ID": "Mode", "Value": mode}]
    }, timeout=30)
    ok = r.status_code == 200
    resp = r.json() if ok else {}
    status = resp.get("Status", "?")
    params = resp.get("Parameter", [])
    print(f"  [AI Server] Set engine Mode={mode} -> HTTP {r.status_code} Status={status} {params}")
    time.sleep(3)  # wait for model reload
    return ok and status == "1"


def set_dealer_mode(mode):
    """Switch AIVisionServerDealer transport mode."""
    r = requests.post(ADMIN_URL, json={
        "Action": "SetParameter",
        "Parameter": [{"ID": "Mode", "Value": mode}]
    }, verify=False, timeout=5)
    ok = r.status_code == 200
    print(f"  [Dealer] Set transport Mode={mode} -> HTTP {r.status_code}")
    time.sleep(0.5)
    return ok


def read_cpu_percent():
    """Read system-wide CPU usage from /proc/stat (non-blocking snapshot)."""
    try:
        with open("/proc/stat", "r") as f:
            line = f.readline()
        parts = line.split()[1:]
        vals = [int(x) for x in parts]
        idle = vals[3] + vals[4]
        total = sum(vals)
        return idle, total
    except Exception:
        return 0, 0


def read_mem_usage_kb():
    """Read available memory info from /proc/meminfo."""
    try:
        with open("/proc/meminfo", "r") as f:
            lines = f.readlines()
        mem = {}
        for line in lines:
            if ":" in line:
                key, val = line.split(":", 1)
                mem[key.strip()] = int(val.strip().split()[0])
        total = mem.get("MemTotal", 0)
        available = mem.get("MemAvailable", mem.get("MemFree", 0))
        used = total - available
        return used, total
    except Exception:
        return 0, 1


class ResourceMonitor:
    """Background thread that samples CPU and RAM usage."""

    def __init__(self):
        self.samples_cpu = []
        self.samples_mem = []
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        prev_idle, prev_total = read_cpu_percent()
        self._prev_idle = prev_idle
        self._prev_total = prev_total
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _loop(self):
        while not self._stop.is_set():
            time.sleep(1.0)
            idle, total = read_cpu_percent()
            d_idle = idle - self._prev_idle
            d_total = total - self._prev_total
            self._prev_idle = idle
            self._prev_total = total
            cpu_pct = 0.0
            if d_total > 0:
                cpu_pct = (1.0 - d_idle / d_total) * 100.0
            self.samples_cpu.append(cpu_pct)

            used_kb, total_kb = read_mem_usage_kb()
            self.samples_mem.append(used_kb / 1024.0)  # MB

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)

    def summary(self):
        n = len(self.samples_cpu)
        if n == 0:
            return {"cpu_avg": 0, "cpu_max": 0, "mem_avg_mb": 0, "mem_max_mb": 0}
        return {
            "cpu_avg": sum(self.samples_cpu) / n,
            "cpu_max": max(self.samples_cpu),
            "mem_avg_mb": sum(self.samples_mem) / n,
            "mem_max_mb": max(self.samples_mem),
        }


# ==================== Core test logic ====================
def run_mode_test(ctx, engine_mode, transport_mode, image_files, num_images):
    """Run a single (engine_mode, transport_mode) combination. Returns stats dict."""
    print(f"\n  --- {engine_mode}/{transport_mode}: {num_images} images (+{WARMUP_IMAGES} warmup) ---")

    # Set transport mode
    if not set_dealer_mode(transport_mode):
        print(f"  [ERROR] Failed to set dealer mode to {transport_mode}")
        return None

    # Setup ZMQ sockets
    result_sub = ctx.socket(zmq.SUB)
    result_sub.setsockopt(zmq.SUBSCRIBE, b"")
    result_sub.setsockopt(zmq.LINGER, 0)
    result_sub.connect(RESULT_SUB_ENDPOINT)

    data_pub = ctx.socket(zmq.PUB)
    data_pub.setsockopt(zmq.LINGER, 0)
    data_pub.bind(IMAGE_PUB_ENDPOINT)
    time.sleep(1.0)

    # Drain stale results
    while True:
        try:
            result_sub.recv_multipart(zmq.NOBLOCK)
        except zmq.Again:
            break

    latencies = []
    detection_counts = []
    failures = 0

    for i in range(WARMUP_IMAGES + num_images):
        fpath = image_files[i % len(image_files)]
        fname = os.path.basename(fpath)
        with open(fpath, "rb") as f:
            img_bytes = f.read()

        header = {
            "ts_sec": int(time.time()),
            "ts_nsec": int((time.time() % 1) * 1e9),
            "pair_id": i,
            "part": "",
            "resolution": "1920,1080",
            "format": "Mono",
        }

        is_warmup = i < WARMUP_IMAGES
        label = "warmup" if is_warmup else f"img-{i - WARMUP_IMAGES + 1:03d}"

        t0 = time.time()
        data_pub.send_json(header, zmq.SNDMORE)
        data_pub.send(img_bytes)

        try:
            result_sub.RCVTIMEO = RESULT_TIMEOUT_S * 1000
            parts = result_sub.recv_multipart()
            t1 = time.time()
            latency = t1 - t0

            result = json.loads(parts[-1].decode("utf-8")) if len(parts) > 1 else {}
            dets = result.get("Result", [])
            n_det = len(dets)
            status = result.get("Status", "?")
            txid = result.get("TransactionID", "?")

            if is_warmup:
                print(f"    [{label}] {fname} -> {n_det} dets ({latency:.2f}s) [warmup, excluded]")
            else:
                latencies.append(latency)
                detection_counts.append(n_det)
                print(f"    [{label}] {fname} -> {n_det} dets, status={status} ({latency:.3f}s)")
        except zmq.Again:
            if not is_warmup:
                failures += 1
                latencies.append(RESULT_TIMEOUT_S)
                detection_counts.append(0)
            print(f"    [{label}] {fname} -> TIMEOUT ({RESULT_TIMEOUT_S}s)")

    data_pub.close()
    result_sub.close()
    time.sleep(0.3)

    if not latencies:
        return None

    return {
        "engine": engine_mode,
        "transport": transport_mode,
        "n_images": len(latencies),
        "n_failures": failures,
        "latencies": latencies,
        "detections": detection_counts,
        "latency_avg": sum(latencies) / len(latencies),
        "latency_min": min(latencies),
        "latency_max": max(latencies),
        "throughput": len(latencies) / sum(latencies) if sum(latencies) > 0 else 0,
        "avg_detections": sum(detection_counts) / len(detection_counts) if detection_counts else 0,
    }


# ==================== Report ====================
def print_report(all_stats, monitor_snapshots):
    print("\n" + "=" * 72)
    print("  CPU PERFORMANCE TEST REPORT")
    print("=" * 72)

    for engine_mode in ["GPU", "CPU"]:
        engine_stats = [s for s in all_stats if s and s["engine"] == engine_mode]
        if not engine_stats:
            continue

        print(f"\n  {engine_mode} Mode:")
        print(f"  {'Transport':<10} {'Images':>6} {'Fail':>5} {'Avg(s)':>8} "
              f"{'Min(s)':>8} {'Max(s)':>8} {'Img/s':>7} {'Avg Det':>8}")
        print(f"  {'-'*10} {'-'*6} {'-'*5} {'-'*8} {'-'*8} {'-'*8} {'-'*7} {'-'*8}")

        for s in engine_stats:
            print(f"  {s['transport']:<10} {s['n_images']:>6} {s['n_failures']:>5} "
                  f"{s['latency_avg']:>8.3f} {s['latency_min']:>8.3f} "
                  f"{s['latency_max']:>8.3f} {s['throughput']:>7.2f} "
                  f"{s['avg_detections']:>8.1f}")

    # GPU vs CPU comparison
    gpu_stats = {s["transport"]: s for s in all_stats if s and s["engine"] == "GPU"}
    cpu_stats = {s["transport"]: s for s in all_stats if s and s["engine"] == "CPU"}

    if gpu_stats and cpu_stats:
        print(f"\n  GPU vs CPU Speedup:")
        print(f"  {'Transport':<10} {'GPU avg(s)':>12} {'CPU avg(s)':>12} {'Speedup':>10}")
        print(f"  {'-'*10} {'-'*12} {'-'*12} {'-'*10}")
        for mode in TRANSPORT_MODES:
            if mode in gpu_stats and mode in cpu_stats:
                gpu_avg = gpu_stats[mode]["latency_avg"]
                cpu_avg = cpu_stats[mode]["latency_avg"]
                speedup = cpu_avg / gpu_avg if gpu_avg > 0 else 0
                print(f"  {mode:<10} {gpu_avg:>12.3f} {cpu_avg:>12.3f} {speedup:>9.1f}x")

    # Resource usage
    if monitor_snapshots:
        print(f"\n  Resource Usage:")
        print(f"  {'Phase':<12} {'CPU avg%':>10} {'CPU max%':>10} {'RAM avg(MB)':>13} {'RAM max(MB)':>13}")
        print(f"  {'-'*12} {'-'*10} {'-'*10} {'-'*13} {'-'*13}")
        for label, snap in monitor_snapshots:
            print(f"  {label:<12} {snap['cpu_avg']:>10.1f} {snap['cpu_max']:>10.1f} "
                  f"{snap['mem_avg_mb']:>13.0f} {snap['mem_max_mb']:>13.0f}")

    print("\n" + "=" * 72)


# ==================== Main ====================
def main():
    parser = argparse.ArgumentParser(description="CPU Performance Test for AIVisionServerDealer")
    parser.add_argument("--images", type=int, default=10, help="Images per mode (default: 10)")
    parser.add_argument("--gpu-only", action="store_true", help="Only test GPU mode")
    parser.add_argument("--cpu-only", action="store_true", help="Only test CPU mode")
    parser.add_argument("--no-gpu-compare", action="store_true",
                        help="Skip GPU baseline (alias for --cpu-only)")
    args = parser.parse_args()

    if args.no_gpu_compare:
        args.cpu_only = True

    engine_modes = []
    if not args.cpu_only:
        engine_modes.append("GPU")
    if not args.gpu_only:
        engine_modes.append("CPU")

    num_images = args.images

    # Find test images
    image_files = sorted(glob.glob(os.path.join(IMAGES_DIR, "*.bmp")))
    if not image_files:
        print(f"[ERROR] No images found at {IMAGES_DIR}")
        sys.exit(1)
    print(f"[Test] Found {len(image_files)} images at {IMAGES_DIR}")
    print(f"[Test] Engine modes: {engine_modes}")
    print(f"[Test] Transport modes: {TRANSPORT_MODES}")
    print(f"[Test] Images per mode: {num_images} (+{WARMUP_IMAGES} warmup)")

    ctx = zmq.Context()
    all_stats = []
    monitor_snapshots = []
    original_engine = None

    try:
        for engine_mode in engine_modes:
            print(f"\n{'='*72}")
            print(f"  Switching AI server to {engine_mode} mode")
            print(f"{'='*72}")

            if not set_ai_engine_mode(engine_mode):
                print(f"[ERROR] Failed to set AI server to {engine_mode}")
                continue

            monitor = ResourceMonitor()
            monitor.start()

            for transport_mode in TRANSPORT_MODES:
                stats = run_mode_test(ctx, engine_mode, transport_mode,
                                      image_files, num_images)
                if stats:
                    all_stats.append(stats)

            monitor.stop()
            snap = monitor.summary()
            monitor_snapshots.append((engine_mode, snap))
            print(f"\n  [{engine_mode}] Resource: CPU avg={snap['cpu_avg']:.1f}% "
                  f"max={snap['cpu_max']:.1f}%, RAM avg={snap['mem_avg_mb']:.0f}MB")

        # Restore GPU mode
        if "GPU" in engine_modes:
            original_engine = "GPU"
        if original_engine and original_engine != engine_modes[-1]:
            print(f"\n[Test] Restoring AI server to {original_engine} mode...")
            set_ai_engine_mode(original_engine)

    finally:
        ctx.term()

    print_report(all_stats, monitor_snapshots)

    # Exit code: 0 if all tests passed, 1 if any failures
    total_failures = sum(s["n_failures"] for s in all_stats if s)
    if total_failures > 0:
        print(f"\n[WARN] {total_failures} image(s) timed out")
        sys.exit(1)
    print("\n[INFO] All tests completed successfully")
    sys.exit(0)


if __name__ == "__main__":
    main()
