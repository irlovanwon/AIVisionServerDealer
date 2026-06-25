# AI Test Server — ecid_2026_r1

> **RULE: Do NOT change or update anything in the AI client at `/home/user/Documents/ecid_2026_r1` on EDGE02** (`core_server.py`, `config.py`, and related files).  
> The AI client is an external project and must remain unmodified. See [folder_structure.md](../folder_structure.md).

---

## Overview

The AI test server (`core_server.py`) is a YOLO-based detection server from the **ecid_2026_r1** project. It receives detection requests via ZMQ REP on TCP port 5555 and returns JSON detection results. Use this server to test the AIVisionServerDealer pipeline end-to-end with a real AI backend.

---

## Server Location

| Item | Value |
|------|-------|
| Host | EDGE02 (LAN: `192.168.0.128`, Tailscale: `100.69.131.6`) |
| Project Path | `/home/user/Documents/ecid_2026_r1` |
| Entry Point | `core_server.py` |
| Python venv | `.venv/` (inside project folder) |

> **Note:** The test script `zmq_test_2.py` references `192.168.0.18` — update `SERVER_HOST` to match the target machine's LAN IP before running.

---

## Startup

```bash
# SSH into EDGE02
ssh user@100.69.131.6

# Activate the AI server
cd /home/user/Documents/ecid_2026_r1
source .venv/bin/activate
python3 core_server.py
```

The server will:
- Load YOLO weights (`./yolo_weights/ecid_26_20260518.pt`)
- Bind ZMQ REP on `tcp://*:5555` (detection data plane)
- Start HTTP server on port `8000` (config/health control plane)
- Bind ZMQ PULL on `tcp://*:5556` (display/result sink)

---

## Key Configuration (config.py)

| Parameter | Value | Description |
|-----------|-------|-------------|
| `CORE_ZMQ_REP_BIND` | `tcp://*:5555` | ZMQ REP socket for detection requests |
| `CORE_HTTP_PORT` | `8000` | HTTP config/health endpoint |
| `AI_DEALER_ID` | `AIModule001` | Dealer ID returned in responses |
| `YOLO_DEVICE` | `cpu` | Device for inference (`cpu` / `0` / `cuda:0`) |
| `YOLO_CONF` | `0.25` | Confidence threshold |

---

## Protocol

The AI server uses **ZMQ REP** (reply) pattern. Clients connect with **ZMQ REQ** (request) or **ZMQ DEALER**.

### Request Frame (JSON)

```json
{
    "TransactionID": "Edge01_20260623-120000-000",
    "DealerID": "Edge01",
    "Timestamp": "20260623-120000-000",
    "Mode": "File",
    "Data": [{
        "URI": "/home/user/Documents/ecid_2026_r1/data/test.jpg",
        "FileName": "test.jpg",
        "Resolution": "",
        "Format": "RGB"
    }]
}
```

> **Mode="File"** is required for the AI test server. The server reads images from disk via the `URI` field. Mode="Stream" (inline binary) is not yet supported by the AI server.

### Response Frame (JSON)

```json
{
    "TransactionID": "Edge01_20260623-120000-000",
    "DealerID": "AIModule001",
    "Result": [
        {
            "LabelID": "...",
            "Confidence": "0.95",
            "FileName": "test.jpg",
            "Coordinates": "x1,y1,x2,y2,...",
            "OCR": "",
            "TimestampStart": "...",
            "TimestampEnd": "..."
        }
    ],
    "TimestampReceived": "...",
    "TimestampReplied": "..."
}
```

---

## Testing with zmq_test_2.py

The `zmq_test_2.py` script verifies that the AI server is operational by sending a single detection request and printing the response.

### Prerequisites

1. The AI server (`core_server.py`) must be running.
2. A test image must exist at the path specified by `IMAGE_PATH` (default: `/home/user/Documents/ecid_2026_r1/data/test.jpg`).

### Usage

```bash
cd /home/user/Documents/ecid_2026_r1
source .venv/bin/activate

# Edit zmq_test_2.py if needed:
#   SERVER_HOST = "192.168.0.128"   # EDGE02 LAN IP
#   IMAGE_PATH  = "/home/user/Documents/ecid_2026_r1/data/test.jpg"

python3 /path/to/AITest/zmq_test_2.py
```

### Expected Output

```
Connecting to tcp://192.168.0.128:5555 ...
-> /detect uri=/home/user/Documents/ecid_2026_r1/data/test.jpg
{
  "TransactionID": "Edge01_...",
  "DealerID": "AIModule001",
  "Result": [ ... ],
  ...
}
```

---

## Integration with AIVisionServerDealer

To connect AIVisionServerDealer to the real AI test server, configure `api1a` in `config/default.json`:

```json
"api1a": {
    "transport": "tcp",
    "endpoint_local": "ipc:///tmp/ai_vision_dealer_detection",
    "endpoint_remote": "tcp://192.168.0.128:5555",
    "identity": "ai_vision_server_dealer",
    "sndhwm": 10,
    "rcvhwm": 10,
    "poll_timeout_ms": 100
}
```

> **Important:** Use `Mode="File"` (not `"Stream"`) when testing with the AI test server, since the AI server reads images from disk via URI. The `core_server.py` does not yet handle 2-part multipart messages with inline binary payloads.

---

## Related Documents

| Document | Description |
|----------|-------------|
| [../server.md](../server.md) | EDGE01/EDGE02 connection details |
| [../API.md](../API.md) | API reference (API1a/1b/2) |
| [../frame.md](../frame.md) | Frame specification for all API boundaries |
