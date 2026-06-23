#!/usr/bin/env python3
"""
Stress Test for AIVisionServerDealer + AI Server (ecid_2026_r1)

Continuously streams images through the full pipeline at maximum throughput.
Monitors CPU and RAM for both AIVisionServerDealer and AI Server processes.
Tests all 3 modes (Binary, File, Http) with configurable duration.

Usage:
  python3 tests/test_stress.py [--duration 60] [--mode Binary] [--images-dir PATH]
"""

import zmq
import json
import time
import os
import sys
import glob
import argparse
import threading
import subprocess
import requests
import urllib3
from collections import deque

urllib3.disable_warnings()

ADMIN_URL = "https://127.0.0.1:8445/admin"
IMAGE_PUB = "ipc:///tmp/sc_pub_stereo_image"
RESULT_SUB = "ipc:///tmp/ai_vision_dealer_result"
IMAGES_DIR = "/home/user/ECIDS/AITestImages/Task1"
CLK_TCK = os.sysconf(os.sysconf_names['SC_CLK_TCK'])


def find_pid(pattern):
    try:
        out = subprocess.check_output(
            ["ps", "aux"], text=True
        )
        for line in out.strip().split("\n"):
            if pattern in line and "grep" not in line and "bash -c" not in line:
                return int(line.split()[1])
    except Exception:
        pass
    return None


def read_cpu_rss(pid):
    """Return (cpu_jiffies, rss_kb) for a process."""
    try:
        with open(f"/proc/{pid}/stat") as f:
            parts = f.read().split()
        utime = int(parts[13])
        stime = int(parts[14])
        rss_pages = int(parts[23])
        return utime + stime, rss_pages * 4  # pages to KB
    except Exception:
        return 0, 0


class ResourceMonitor:
    """Monitors CPU% and RSS MB for multiple PIDs every second."""

    def __init__(self, targets):
        # targets: dict label -> pid
        self.targets = targets
        self.samples = []  # list of (timestamp, {label: (cpu_pct, rss_mb)}, sys_cpu_pct)
        self._stop = threading.Event()
        self._thread = None
        self._prev = {}

    def start(self):
        for label, pid in self.targets.items():
            jif, rss = read_cpu_rss(pid)
            self._prev[label] = (jif, rss, time.time())
        self._prev_sys = self._read_sys_cpu()
        self._prev_t = time.time()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _read_sys_cpu(self):
        try:
            with open("/proc/stat") as f:
                line = f.readline().split()[1:]
            vals = [int(x) for x in line]
            idle = vals[3] + vals[4]
            total = sum(vals)
            return idle, total
        except:
            return 0, 0

    def _loop(self):
        while not self._stop.is_set():
            time.sleep(1.0)
            now = time.time()
            dt = now - self._prev_t

            row = {}
            for label, pid in self.targets.items():
                jif, rss = read_cpu_rss(pid)
                prev_jif, _, _ = self._prev.get(label, (0, 0, now))
                cpu_pct = (jif - prev_jif) / CLK_TCK / dt * 100 if dt > 0 else 0
                row[label] = (cpu_pct, rss / 1024.0)  # MB
                self._prev[label] = (jif, rss, now)

            sys_idle, sys_total = self._read_sys_cpu()
            prev_idle, prev_total = self._prev_sys
            d_idle = sys_idle - prev_idle
            d_total = sys_total - prev_total
            sys_cpu = (1.0 - d_idle / d_total) * 100 if d_total > 0 else 0
            self._prev_sys = (sys_idle, sys_total)

            self.samples.append((now, row, sys_cpu))
            self._prev_t = now

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)

    def summary(self):
        if not self.samples:
            return {}
        result = {"sys_cpu": []}
        for label in self.targets:
            result[label] = {"cpu": [], "rss": []}
        for _, row, sys_cpu in self.samples:
            result["sys_cpu"].append(sys_cpu)
            for label in self.targets:
                if label in row:
                    result[label]["cpu"].append(row[label][0])
                    result[label]["rss"].append(row[label][1])
        out = {}
        out["sys_cpu_avg"] = sum(result["sys_cpu"]) / len(result["sys_cpu"])
        out["sys_cpu_max"] = max(result["sys_cpu"])
        for label in self.targets:
            cpus = result[label]["cpu"]
            rsss = result[label]["rss"]
            out[f"{label}_cpu_avg"] = sum(cpus) / len(cpus) if cpus else 0
            out[f"{label}_cpu_max"] = max(cpus) if cpus else 0
            out[f"{label}_rss_avg"] = sum(rsss) / len(rsss) if rsss else 0
            out[f"{label}_rss_max"] = max(rsss) if rsss else 0
        return out


def set_dealer_mode(mode):
    requests.post(ADMIN_URL, json={
        "Action": "SetParameter",
        "Parameter": [{"ID": "Mode", "Value": mode}]
    }, verify=False, timeout=5)
    time.sleep(0.5)


def drain_results(sub):
    while True:
        try:
            sub.recv_multipart(zmq.NOBLOCK)
        except zmq.Again:
            break


def run_stress(ctx, mode, image_files, duration_s):
    """Stream images continuously for duration_s. Return stats."""
    dealer_pid = find_pid("ai_vision_server_dealer --config")
    ai_pid = find_pid("core_server.py")
    print(f"  PIDs: Dealer={dealer_pid} AI={ai_pid}")

    monitor = ResourceMonitor({
        "dealer": dealer_pid,
        "ai": ai_pid,
    })

    result_sub = ctx.socket(zmq.SUB)
    result_sub.setsockopt(zmq.SUBSCRIBE, b"")
    result_sub.setsockopt(zmq.LINGER, 0)
    result_sub.connect(RESULT_SUB)

    data_pub = ctx.socket(zmq.PUB)
    data_pub.setsockopt(zmq.LINGER, 0)
    data_pub.bind(IMAGE_PUB)
    time.sleep(1.0)
    drain_results(result_sub)

    set_dealer_mode(mode)
    monitor.start()

    latencies = []
    count = 0
    failures = 0
    idx = 0
    t_start = time.time()

    print(f"  Streaming {mode} for {duration_s}s...", end="", flush=True)

    while time.time() - t_start < duration_s:
        fpath = image_files[idx % len(image_files)]
        idx += 1
        with open(fpath, "rb") as f:
            img_bytes = f.read()

        header = {
            "ts_sec": int(time.time()),
            "ts_nsec": 0,
            "pair_id": count,
            "part": "",
            "resolution": "1920,1080",
            "format": "Mono",
        }

        t0 = time.time()
        data_pub.send_json(header, zmq.SNDMORE)
        data_pub.send(img_bytes)

        try:
            result_sub.RCVTIMEO = 30000
            parts = result_sub.recv_multipart()
            latency = time.time() - t0
            latencies.append(latency)
            count += 1
        except zmq.Again:
            failures += 1
            print(f"\n  TIMEOUT at image {count}", end="", flush=True)

    elapsed = time.time() - t_start
    monitor.stop()

    data_pub.close()
    result_sub.close()
    time.sleep(0.3)

    stats = monitor.summary()

    lat_sorted = sorted(latencies) if latencies else [0]
    return {
        "mode": mode,
        "duration": elapsed,
        "images": count,
        "failures": failures,
        "throughput": count / elapsed if elapsed > 0 else 0,
        "latency_avg": sum(latencies) / len(latencies) if latencies else 0,
        "latency_p50": lat_sorted[len(lat_sorted) // 2],
        "latency_p95": lat_sorted[int(len(lat_sorted) * 0.95)] if len(lat_sorted) > 1 else 0,
        "latency_max": max(latencies) if latencies else 0,
        **stats,
    }


def print_report(all_stats):
    print("\n" + "=" * 88)
    print("  STRESS TEST REPORT")
    print("=" * 88)

    for s in all_stats:
        print(f"\n  Mode: {s['mode']}")
        print(f"  {'Metric':<24} {'Value':>12}")
        print(f"  {'-'*24} {'-'*12}")
        print(f"  {'Duration':.<24} {s['duration']:>10.1f} s")
        print(f"  {'Images processed':.<24} {s['images']:>12}")
        print(f"  {'Failures':.<24} {s['failures']:>12}")
        print(f"  {'Throughput':.<24} {s['throughput']:>10.2f} img/s")
        print(f"  {'Latency avg':.<24} {s['latency_avg']:>10.3f} s")
        print(f"  {'Latency p50':.<24} {s['latency_p50']:>10.3f} s")
        print(f"  {'Latency p95':.<24} {s['latency_p95']:>10.3f} s")
        print(f"  {'Latency max':.<24} {s['latency_max']:>10.3f} s")
        print()
        print(f"  {'Resource':<24} {'Dealer':>12} {'AI Server':>12} {'System':>12}")
        print(f"  {'-'*24} {'-'*12} {'-'*12} {'-'*12}")
        print(f"  {'CPU avg %':.<24} {s.get('dealer_cpu_avg',0):>11.1f}% {s.get('ai_cpu_avg',0):>11.1f}% {s.get('sys_cpu_avg',0):>11.1f}%")
        print(f"  {'CPU max %':.<24} {s.get('dealer_cpu_max',0):>11.1f}% {s.get('ai_cpu_max',0):>11.1f}% {s.get('sys_cpu_max',0):>11.1f}%")
        print(f"  {'RAM avg MB':.<24} {s.get('dealer_rss_avg',0):>12.0f} {s.get('ai_rss_avg',0):>12.0f}")
        print(f"  {'RAM max MB':.<24} {s.get('dealer_rss_max',0):>12.0f} {s.get('ai_rss_max',0):>12.0f}")

    if len(all_stats) > 1:
        print(f"\n{'='*88}")
        print("  COMPARISON")
        print(f"{'='*88}")
        print(f"  {'Mode':<10} {'Img/s':>7} {'Lat avg':>8} {'D-CPU%':>7} {'AI-CPU%':>8} {'D-RAM':>7} {'AI-RAM':>7}")
        print(f"  {'-'*10} {'-'*7} {'-'*8} {'-'*7} {'-'*8} {'-'*7} {'-'*7}")
        for s in all_stats:
            print(f"  {s['mode']:<10} {s['throughput']:>7.2f} {s['latency_avg']:>8.3f} "
                  f"{s.get('dealer_cpu_avg',0):>6.1f} {s.get('ai_cpu_avg',0):>7.1f} "
                  f"{s.get('dealer_rss_avg',0):>7.0f} {s.get('ai_rss_avg',0):>7.0f}")

    print(f"\n{'='*88}")


def main():
    parser = argparse.ArgumentParser(description="Stress Test — AIVisionServerDealer + AI Server")
    parser.add_argument("--duration", type=int, default=60, help="Seconds per mode (default: 60)")
    parser.add_argument("--mode", type=str, default=None, choices=["Binary", "File", "Http"],
                        help="Test single mode only")
    parser.add_argument("--images-dir", type=str, default=IMAGES_DIR)
    args = parser.parse_args()

    files = sorted(glob.glob(os.path.join(args.images_dir, "*.bmp")))
    if not files:
        print(f"[ERROR] No images at {args.images_dir}")
        sys.exit(1)
    print(f"[Stress] {len(files)} images, {args.duration}s per mode")

    modes = [args.mode] if args.mode else ["Binary", "File", "Http"]

    # Verify processes are running
    dealer_pid = find_pid("ai_vision_server_dealer --config")
    ai_pid = find_pid("core_server.py")
    if not dealer_pid:
        print("[ERROR] AIVisionServerDealer not running")
        sys.exit(1)
    if not ai_pid:
        print("[ERROR] AI server (core_server.py) not running")
        sys.exit(1)
    print(f"[Stress] Dealer PID={dealer_pid}, AI PID={ai_pid}")

    ctx = zmq.Context()
    all_stats = []

    for mode in modes:
        print(f"\n{'='*60}")
        print(f"  STRESS TEST: {mode} mode ({args.duration}s)")
        print(f"{'='*60}")
        s = run_stress(ctx, mode, files, args.duration)
        all_stats.append(s)
        print(f" Done — {s['images']} images, {s['throughput']:.2f} img/s")
        time.sleep(3)

    ctx.term()
    print_report(all_stats)


if __name__ == "__main__":
    main()
