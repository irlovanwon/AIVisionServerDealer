#!/usr/bin/env python3
import os, json, time, zmq
from datetime import datetime

# -------- CONFIG --------
SERVER_HOST = "192.168.0.18"
ZMQ_PORT    = 5555
DEALER_ID   = "Edge01"
IMAGE_PATH  = "/home/user/Documents/ecid_2026_r1/data/test.jpg"
USE_FILE_URI_SCHEME = False      # <- bare path by default
SNDTIMEO_MS = 5000
RCVTIMEO_MS = 15000
# ------------------------

def ts(): return datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]

def build_payload(uri: str):
    t = ts()
    return {
        "TransactionID": f"{DEALER_ID}_{t}",
        "DealerID": DEALER_ID,
        "Timestamp": t,
        "Mode": "File",
        "Data": [{
            "URI": uri,
            "FileName": os.path.basename(IMAGE_PATH),
            "Resolution": "",
            "Format": "RGB"
        }]
    }

def main():
    if USE_FILE_URI_SCHEME:
        uri = f"file://{IMAGE_PATH}" if IMAGE_PATH.startswith("/") else f"file:///{IMAGE_PATH}"
    else:
        uri = IMAGE_PATH

    endpoint = f"tcp://{SERVER_HOST}:{ZMQ_PORT}"
    payload  = build_payload(uri)

    ctx = zmq.Context.instance()
    s = ctx.socket(zmq.REQ)
    s.setsockopt(zmq.LINGER, 0)
    s.setsockopt(zmq.SNDTIMEO, SNDTIMEO_MS)
    s.setsockopt(zmq.RCVTIMEO, RCVTIMEO_MS)

    print(f"Connecting to {endpoint} ...")
    s.connect(endpoint)
    time.sleep(0.1)

    print(f"-> /detect uri={uri}")
    try:
        s.send_json(payload)
        rep = s.recv_json()
    except zmq.error.Again:
        print("ERROR: Timeout waiting for server reply.")
        return 2
    finally:
        s.close(0)

    print(json.dumps(rep, indent=2, ensure_ascii=False))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())