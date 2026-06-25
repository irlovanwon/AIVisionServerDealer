#!/usr/bin/env python3
"""
Batch AI detection test — sends all Task1/Task2 images to the AI server
via ZMQ REQ and saves results as individual JSON files.
"""

import os, json, time, zmq, sys, glob
from datetime import datetime

SERVER_HOST = "127.0.0.1"
ZMQ_PORT    = 5555
DEALER_ID   = "BatchTest"
IMAGE_ROOT  = "/home/user/ECIDS/AITestImages"
RESULT_DIR  = "/home/user/ECIDS/AITestImages/results"
SNDTIMEO_MS = 10000
RCVTIMEO_MS = 30000

def ts():
    return datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]

def build_payload(image_path):
    t = ts()
    return {
        "TransactionID": f"{DEALER_ID}_{os.path.basename(image_path)}_{t}",
        "DealerID": DEALER_ID,
        "Timestamp": t,
        "Mode": "File",
        "Data": [{
            "URI": image_path,
            "FileName": os.path.basename(image_path),
            "Resolution": "",
            "Format": "RGB"
        }]
    }

def main():
    os.makedirs(RESULT_DIR, exist_ok=True)
    endpoint = f"tcp://{SERVER_HOST}:{ZMQ_PORT}"

    tasks = {
        "Task1": os.path.join(IMAGE_ROOT, "Task1"),
        "Task2": os.path.join(IMAGE_ROOT, "Task2"),
    }

    all_images = []
    for task_name, task_dir in tasks.items():
        if not os.path.isdir(task_dir):
            print(f"[WARN] {task_dir} not found, skipping")
            continue
        bmps = sorted(glob.glob(os.path.join(task_dir, "*.bmp")))
        all_images.extend([(task_name, p) for p in bmps])

    total = len(all_images)
    print(f"[INFO] {total} images to process")
    print(f"[INFO] Connecting to {endpoint}")

    ctx = zmq.Context.instance()
    sock = ctx.socket(zmq.REQ)
    sock.setsockopt(zmq.LINGER, 0)
    sock.setsockopt(zmq.SNDTIMEO, SNDTIMEO_MS)
    sock.setsockopt(zmq.RCVTIMEO, RCVTIMEO_MS)
    sock.connect(endpoint)
    time.sleep(0.2)

    ok = 0
    fail = 0
    t_start = time.time()

    for i, (task_name, image_path) in enumerate(all_images, 1):
        fname = os.path.basename(image_path)
        base = os.path.splitext(fname)[0]
        result_path = os.path.join(RESULT_DIR, f"{task_name}_{base}.json")
        payload = build_payload(image_path)

        elapsed = time.time() - t_start
        rate = i / elapsed if elapsed > 0 else 0
        eta = (total - i) / rate if rate > 0 else 0
        print(f"[{i}/{total}] {task_name}/{fname} ({rate:.1f} img/s, ETA {eta:.0f}s)", end=" ... ", flush=True)

        try:
            sock.send_json(payload)
            rep = sock.recv_json()
            with open(result_path, "w") as f:
                json.dump(rep, f, indent=2, ensure_ascii=False)
            n_det = len(rep.get("Result", []))
            print(f"OK ({n_det} detections)")
            ok += 1
        except zmq.error.Again:
            print("TIMEOUT")
            fail += 1
        except Exception as e:
            print(f"ERROR: {e}")
            fail += 1

    elapsed = time.time() - t_start
    sock.close()

    print(f"\n{'='*60}")
    print(f"BATCH TEST COMPLETE")
    print(f"  Total: {total}")
    print(f"  OK:    {ok}")
    print(f"  FAIL:  {fail}")
    print(f"  Time:  {elapsed:.1f}s ({ok/elapsed:.1f} img/s)" if elapsed > 0 else "")
    print(f"  Results saved to: {RESULT_DIR}")
    print(f"{'='*60}")

    return 0 if fail == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
