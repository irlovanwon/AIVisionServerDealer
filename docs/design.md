# AIVisionServerDealer — System Architecture & Design

## 1. Overview

AIVisionServerDealer is a C++17 module that bridges a **data server** and **AI client(s)** to handle vision and sensor data. It subscribes to visual, geometric, and sensor data from the data server, forwards it to the AI client for inference, and exposes AI detection results and configuration through HTTPS command interfaces. A separate HTTPS admin channel allows configuration of the AI client.

### Key Characteristics

- **Single-process, multi-threaded** C++17 application
- **Three-layer dealer architecture** — API2 (data server) ↔ Core ↔ API1 (AI client)
- **3-mode data transfer system** — `File` (local file URI), `Http` (HTTP/HTTPS URI), `Binary` (default; raw image data via ZMQ multipart)
- **API1a** — AI detection over ZMQ DEALER/DEALER (data in → results out)
- **API1b** — Admin commands over HTTPS (parameter get/set, status)
- **API2a** — Subscribe data from the data server via ZMQ SUB; expanded data types — Image, Depth Map, 3D Point Cloud, IMU, Magnetometer, Barometer
- **API2b** — HTTPS server for command get/set with the data server (same frame format as API1b admin)
- **Internal `std::shared_ptr` dataflow** — no memory copy between threads
- **ZMQ tcp/ipc selectable** per ZMQ module (API2a transport also selectable from the command API)
- **JPEG encoding** applied at the API2 boundary for image data before transfer onward to the AI client

See also: [API.md](API.md) | [frame.md](frame.md) | [config.md](config.md) | [parameter.md](parameter.md) | [folder_structure.md](folder_structure.md) | [server.md](server.md)

---

## 2. Tech Stack

| Layer | Technology |
|-------|-----------|
| Core Language | C++17 |
| Build System | CMake 3.16+ |
| Messaging | ZeroMQ (ZMQ) — DEALER/DEALER + SUB, IPC + TCP |
| Command Protocol | HTTPS (TLS mandatory, OpenSSL) — used by both API1b (admin) and API2b (data server command get/set) |
| Serialization | JSON (nlohmann-json for control/admin/command), ZMQ frames for image & result data |
| Image Handling | OpenCV (incl. JPEG encode/decode) |
| Testing | Google Test (gtest) |
| Config Format | JSON, `config/` directory |
| Target Platform | Linux aarch64 (EDGE01/EDGE02), Linux x86_64 |

---

## 3. Module Architecture

```
   │              │   API2a (ZMQ SUB — data in:                       ┌─────────────────────────────────┐   API1a (ZMQ DEALER/DEALER — detection)   ┌──────────┐
   │              │   Image, Depth Map, Point Cloud,                  │     AIVisionServerDealer        │◄─────────────────────────────────────────►│ AI Client│
   │              │   IMU, Magnetometer, Barometer, JSON)             │                                 │   Binary mode: raw image data             │(inference│
   │              │◄─────────────────────────────────────────────────│  ┌───────────────────────────┐  │   File/Http mode: URI reference           └──────────┘
   │  Data Server │   API2a (ZMQ PUB — result out:                    │  │     Core Module           │  │
   │              │◄─────────────────────────────────────────────────│  │  - Ingest pipeline        │  │           API1b (HTTPS — admin)          ┌──────────┐
   │              │    detection results)                             │  │  - Request/response queue │  │◄─────────────────────────────────────────►│  Admin   │
   │              │──────────────────────────────────────────────────►│  │  - Result forwarding      │  │                                            │  Client  │
   │              │   API2b (HTTPS — command get/set, configs)         │  │                           │  │                                           └──────────┘
   │              │◄─────────────────────────────────────────────────►│  └───────────────────────────┘  │
   └──────────────┘                                                    └─────────────────────────────────┘
```

### Three-Layer Dealer Flow

| Layer | Boundary | Protocol | Direction |
|-------|----------|----------|-----------|
| API 1a | Dealer ↔ AI Client (detection) | ZMQ DEALER/DEALER | Bidirectional (data → result) |
| API 1b | Dealer ↔ Admin Client | HTTPS | Request/Response |
| API 2a | Data Server ↔ Dealer (data in) | ZMQ SUB | Data Server → Dealer |
| API 2b | Dealer ↔ Data Server (command get/set) | HTTPS | Request/Response |

### End-to-End Data Path

```
Data Server --(API2a SUB, ZMQ SUB, JPEG-encoded image data / sensor data)--> Core
Core        --(API1a, ZMQ DEALER, detection request)--> AI Client
                  Binary mode : raw image data
                  File mode   : local file URI (path)
                  Http mode   : HTTP/HTTPS URI (URL)
AI Client   --(API1a, ZMQ DEALER, detection result)--> Core
Core        --(API2a PUB, ZMQ PUB, detection result)--> Data Server
```

---

## 4. Source Code Structure

See [folder_structure.md](folder_structure.md) for the full directory listing.

### 4.1 Module Map

| Module | Source Dir | Responsibility |
|--------|-----------|----------------|
| **Common** | `common/` | Shared types (`Timestamp`, `Response`), response codes, logging |
| **API1** | `api1/` | API1a ZMQ DEALER (detection), API1b HTTPS server (admin) |
| **API2** | `api2/` | API2a ZMQ SUB (expanded data ingest from data server), API2b HTTPS server (command get/set) |
| **Core** | `core/` | Ingest pipeline, data buffer, request/response queue, result forwarding |
| **Data** | `data/` | Config sync (`ConfigManager`) |

---

## 5. API Contracts

### 5.1 API 1 — Dealer ↔ AI Client

**API 1a — AI Detection (ZMQ DEALER/DEALER):** Both the Dealer and the AI Client use ZMQ DEALER sockets for asynchronous request/response. The Dealer sends a detection request and the AI Client replies with detection results. The request payload depends on the active **Mode**:

| Mode | Request Payload | Notes |
|------|-----------------|-------|
| `Binary` (default) | Raw image data via ZMQ multipart messages | AI client receives raw binary image data |
| `File` | Local file URI (path) | AI client reads the image from disk |
| `Http` | HTTP/HTTPS URI (URL link) | AI client downloads the image |

Supports both `tcp` and `ipc` (selectable via config).

| Property | Value |
|----------|-------|
| Pattern | ZMQ DEALER ↔ DEALER |
| Data | Raw image data (Binary mode) or URI reference (File/Http mode) |
| Transport | `tcp` or `ipc` (configurable) |
| Asynchronous | Yes — both sides send/receive without blocking |

See [API.md §API1a](API.md#api-1a--ai-detection-zmq-dealerdealer) and [frame.md §API1a](frame.md#api-1a--ai-detection-frame-zmq-dealerdealer).

**API 1b — Admin Command (HTTPS):** The Dealer exposes an HTTPS server for admin commands — parameter get/set, mode selection, status checks.

See [API.md §API1b](API.md#api-1b--admin-command-https) and [frame.md §API1b](frame.md#api-1b--admin-frame-https).

### 5.2 API 2 — Dealer ↔ Data Server

**API 2a — Data Subscription & Result Publish (ZMQ SUB + PUB):** The Dealer subscribes to data published by the data server via ZMQ SUB. The channel set covers visual, geometric, sensor/tracking, and other JSON data. Additionally, the Dealer publishes AI detection results back to the data server via ZMQ PUB on a dedicated result endpoint. Supports both `tcp` and `ipc` (selectable via config and from the command API).

| Property | Value |
|----------|-------|
| SUB Role | AIVisionServerDealer = ZMQ Subscriber; Data Server = ZMQ Publisher |
| PUB Role | AIVisionServerDealer = ZMQ Publisher; Data Server = ZMQ Subscriber |
| Data types | Image, Depth Map, 3D Point Cloud, IMU, Magnetometer, Barometer, JSON |
| Transport | `tcp` or `ipc` (configurable; also selectable from command API) |

**API 2b — Command Get/Set (HTTPS):** The Dealer exposes an HTTPS server for command get/set with the data server (e.g., retrieve results, get/set configs). It uses the **same frame format as API1b admin**. The data server sends commands to get/set configs via HTTPS.

| Property | Value |
|----------|-------|
| Pattern | HTTPS request/response (Dealer = server, Data Server = client) |
| Frame format | Same as API1b admin (see [frame.md §API1b](frame.md#api-1b--admin-frame-https)) |
| TLS | Mandatory (OpenSSL) |

> **Change note:** API2b was previously a ZMQ PUB result-publish channel. It is now an HTTPS command get/set interface, consistent with the API1b admin frame design.

See [API.md §API2](API.md#api-2--dealer--data-server) and [frame.md §API2](frame.md#api-2--data-server-frame).

### 5.3 Data Transfer & Encoding Rules

| Boundary | Rule |
|----------|------|
| **API1 (Dealer ↔ AI Client)** | The AI client receives **raw data only**. In `Binary` mode, binary image data is transferred directly via ZMQ multipart messages. |
| **API2 (Data Server ↔ Dealer)** | **JPEG encoding** is applied to image data before transfer onward (toward the AI client). The Dealer ingests JPEG-encoded image frames from the data server. Non-image sensor data (IMU, magnetometer, barometer, etc.) is transferred in its native serialized form. |

---

## 6. Data Types

| Data Type | Category | Description | Channel |
|-----------|----------|-------------|---------|
| Image | Visual & Geometric | Single image (mono or color) for AI detection | API 2a (in) |
| Stereo Image | Visual & Geometric | Paired left + right images per frame | API 2a (in) |
| Depth Map | Visual & Geometric | Per-pixel depth image | API 2a (in) |
| 3D Point Cloud | Visual & Geometric | 3D spatial data (point cloud) | API 2a (in) |
| IMU | Sensor & Tracking | Inertial measurement (accelerometer + gyroscope) | API 2a (in) |
| Magnetometer | Sensor & Tracking | Magnetic field vector | API 2a (in) |
| Barometer | Sensor & Tracking | Atmospheric pressure | API 2a (in) |
| Detection Request | Detection | Data sent to AI client — raw image (Binary mode) or URI (File/Http mode) | API 1a |
| Detection Result | Detection | Label, confidence, coordinates, OCR per detected object | API 1a (out) → API 2b |
| Admin Request | Control | Parameter get/set, mode selection | API 1b |
| Admin Response | Control | Status + parameter echo | API 1b |
| Command Request | Control | Config get/set from data server | API 2b |
| Command Response | Control | Status + config/result echo | API 2b |

See [frame.md](frame.md) for full frame specifications and [API design/Examples](API%20design/Examples/) for example JSON.

---

## 7. Parameters

Every parameter carries an identifier and value. Parameters are sent to the AI client via API1b admin commands.

| Parameter | Type | Description |
|-----------|------|-------------|
| Inference Mode | Enum (`GPU` / `CPU`) | AI inference device selection |
| Confidence Threshold | Float | Minimum confidence for detection results |
| Mode | Enum (`File` / `Http` / `Binary`) | Data transfer mode to AI client. `File`: send local file URI (path), AI reads from disk. `Http`: send HTTP/HTTPS URI (URL), AI downloads. `Binary`: send image data directly via ZMQ multipart messages. **Default: `Binary`** |
| API2a Transport | Enum (`tcp` / `ipc`) | ZMQ transport for API2a (selectable from command API) |

> The single legacy "Stream" mode is replaced by the 3-mode system (`File` / `Http` / `Binary`).

See [parameter.md](parameter.md) for the full parameter list.

---

## 8. Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| 3-mode data transfer system (`File` / `Http` / `Binary`) | Lets the AI client receive data the way it prefers — raw binary by default (lowest latency, no disk/network dependency), or a URI reference when the AI client has disk/web access. Replaces the old single "Stream" mode. |
| JPEG encoding at the API2 boundary | Compresses image data before transfer onward to the AI client, reducing ZMQ bandwidth on the ingest path. |
| ZMQ DEALER/DEALER for AI detection | Fully asynchronous — Dealer and AI Client operate independently; matches the request/response dealer semantics. See [Coding/ZMQ.md](../../Coding/ZMQ.md) DEALER/DEALER pattern |
| ZMQ SUB for data ingest | Data server publishes, Dealer subscribes — supports the expanded data-type set (image, depth, point cloud, IMU, magnetometer, barometer) with extension to stereo (multiple images) |
| HTTPS for both API1b (admin) and API2b (data server command get/set) | Secure, request/response command channels consistent with project standards; API2b reuses the API1b admin frame format |
| ZMQ tcp/ipc selectable per module | Flexible deployment: `ipc` for intra-machine (lowest latency), `tcp` for inter-machine |
| `std::shared_ptr` for internal dataflow | Reduces allocation overhead and avoids unnecessary duplication |
| Lock-free SPSC ring buffer for data queue | Atomic head/tail indices with `alignas(64)` cache-line isolation; drop-newest policy ensures bounded latency under backpressure |
| Zero-copy ZMQ send (`zmq_msg_init_data`) | Eliminates data duplication at ZMQ boundaries — heap-allocated payload owned by ZMQ message deleter lambda |
| Backpressure via `processing_paused_` + acknowledgment | `processing_paused_` atomic auto-pauses the pipeline when unacknowledged results reach a limit; the subscriber/data-server acknowledges via the command interface to decrement the counter and auto-resume |
| `std::condition_variable` for dedicated threads | Dedicated ingest/worker threads wait on a condition variable instead of busy-polling, reducing CPU usage |
| JSON for admin + control, ZMQ frames for image/result | JSON is human-readable for config; binary frames for image payloads |
| Separate API1 and API2 config | Each module can be independently tuned for transport and endpoints |

---

## 9. Response Codes

The C++ `ResponseCode` enum is used internally by the admin/command handler logic:

| Code | Name | Meaning |
|------|------|---------|
| 0 | Success | Operation completed |
| 1 | Error | General failure |
| 2 | NotReady | Precondition not met (e.g., not connected) |
| 3 | AlreadyInit | Already initialized (idempotent success) |
| 4 | InvalidParam | Parameter name or value invalid |
| 5 | Unavailable | Feature not supported |

> **Note:** The JSON `Status` field in API1b admin responses and API2b command responses uses **string** semantics: `"1"` = success, `"0"` = failure. This is separate from the internal C++ enum above and follows the VIATECH API design convention. See [frame.md](frame.md).

---

## 10. Key Constraints

| ID | Constraint |
|----|-----------|
| C1 | API 1a (AI detection) uses ZMQ DEALER/DEALER |
| C2 | API 1b (admin) uses HTTPS with TLS mandatory |
| C3 | API 2a (data in) uses ZMQ SUB — supports expanded data types: Image, Depth Map, 3D Point Cloud, IMU, Magnetometer, Barometer |
| C4 | For stereo cameras, API 2a receives multiple images (left + right) per frame |
| C5 | API 2b (command get/set) uses HTTPS with TLS mandatory — same frame format as API1b admin |
| C6 | ZMQ supports both `tcp` and `ipc` (selectable per ZMQ module via config; API2a transport also selectable from the command API) |
| C7 | Internal dataflow uses `std::shared_ptr` instead of memory copy |
| C8 | Module is fully independent — no direct coupling beyond ZMQ/HTTPS boundaries |
| C9 | Timestamps follow `YYYYMMDD-HHMMSS-sss` format per [Coding/rule.md](../../Coding/rule.md) |
| C10 | All configuration uses JSON format per [Coding/config_rule.md](../../Coding/config_rule.md) |
| C11 | If no AI client is connected to API1a, the Dealer must prevent data overflow and expose a status result indicating the situation |
| C12 | Ingest pipeline uses a lock-free SPSC ring buffer with drop-newest policy (atomics, `alignas(64)` anti-false-sharing) |
| C13 | ZMQ send paths use zero-copy (`zmq_msg_init_data`) to avoid data duplication at API boundaries |
| C14 | Pipeline supports backpressure via `processing_paused_` atomic: when pending unacknowledged results reach a limit, processing auto-pauses; the data server acknowledges via the command interface |
| C15 | API2 applies JPEG encoding to image data before transfer onward to the AI client |
| C16 | Default data transfer Mode is `Binary` |

---

## 11. No-Dealer Handling (API1a)

When no AI client is connected to API1a (ZMQ DEALER/DEALER), the Dealer must avoid data overflow and provide status feedback.

### Mechanism

| Layer | Implementation |
|-------|---------------|
| **Connection detection** | ZMQ socket monitor (`zmq_socket_monitor`) tracks `ZMQ_EVENT_CONNECTED` / `ZMQ_EVENT_DISCONNECTED`; `DetectionDealer::is_client_connected()` exposes the state |
| **Overflow prevention** | `ZMQ_IMMEDIATE=1` prevents message queuing before the peer connects; `ZMQ_DONTWAIT` on `zmq_send` ensures non-blocking send; `ZMQ_SNDHWM` bounds the outbound queue |
| **Pipeline guard** | CoreModule checks `is_client_connected()` before processing each frame; if no client, the frame is consumed (buffer stays healthy) but no detection request is sent |
| **Status return** | When no client is connected, CoreModule exposes a status result via the API2b HTTPS command interface (and API1b admin status) with `Status=0` and `Reason="No AI client connected"` (throttled to every 5 seconds) |

### Status Result Format (No-Client)

Returned via the API2b HTTPS command interface (same frame format as API1b admin) when no AI client is connected:

```json
{
    "TransactionID": "20260614-141631-652-1",
    "DealerID": "Edge001",
    "Status": "0",
    "Reason": "No AI client connected",
    "Result": [],
    "TimestampReplied": "20260614-141632-167"
}
```

Normal AI-client results include `"Status": "1"` for consistency.

---

## 12. Smart Handling

### 12.1 Core Restart Handling (API2a)

**Problem:** When the Data Server (Core) restarts, its ZMQ PUB sockets are destroyed and recreated. The Dealer's ZMQ SUB sockets do not automatically re-establish — IPC socket files become stale, and subscription state may be lost.

**Solution:** Core notifies the Dealer at restart via the API2b HTTPS endpoint with a `"Reconnect"` action. The Dealer tears down and re-creates all ZMQ SUB connections, ensuring clean reconnection.

#### Mechanism

| Step | Actor | Action |
|------|-------|--------|
| 1 | Core (Data Server) | Restarts — destroys old PUB sockets, creates new ones |
| 2 | Core (Data Server) | Sends `Action: "Reconnect"` via HTTPS POST to Dealer's API2b endpoint (port 8446) |
| 3 | AIVisionServerDealer | Receives the request; closes all SUB sockets; re-creates and re-connects to all configured channels; re-applies subscription filters |
| 4 | AIVisionServerDealer | Responds with `Status: "1"` (success) |
| 5 | Core (Data Server) | Resumes publishing data on its PUB sockets |

#### Reconnect Request Frame (Core → Dealer, via API2b HTTPS)

```json
{
    "TransactionID": "20260625-080000-000-1",
    "DealerID": "Edge001",
    "Action": "Reconnect",
    "Parameter": [],
    "Timestamp": "20260625-080000"
}
```

#### Reconnect Response Frame (Dealer → Core)

```json
{
    "TransactionID": "20260625-080000-000-1",
    "DealerID": "Edge001",
    "Status": "1",
    "Parameter": [],
    "TimestampReceived": "20260625-080000",
    "TimestampReplied": "20260625-080001"
}
```

#### Implementation Notes

| Layer | Responsibility |
|-------|---------------|
| **API2b handler** | Maps `Action: "Reconnect"` to `CoreModule::reconnect_sub()` |
| **CoreModule** | Calls `ImageSubscriber::reconnect()`, blocks until reconnection completes |
| **ImageSubscriber** | Closes all ZMQ SUB sockets, re-creates them, re-connects to all configured channel endpoints, re-applies `ZMQ_SUBSCRIBE` filter |

> **Note:** ZMQ's built-in TCP reconnect handles transient network failures automatically. The `Reconnect` action is specifically needed when the Data Server process restarts (IPC socket files are recreated, subscription state must be refreshed).

---

## 13. Development Principles

Follow the development rules in [Coding/](../../Coding/):

| Rule | Document |
|------|----------|
| General coding | [Coding/rule.md](../../Coding/rule.md) |
| ZMQ programming | [Coding/ZMQ.md](../../Coding/ZMQ.md) |
| HTTP/HTTPS programming | [Coding/HTTP.md](../../Coding/HTTP.md) |
| Configuration format | [Coding/config_rule.md](../../Coding/config_rule.md) |
| Folder structure | [Coding/folder_structure.md](../../Coding/folder_structure.md) |
| File signatures | [Coding/signature.md](../../Coding/signature.md) |
| Testing | [Coding/testing_rule.md](../../Coding/testing_rule.md) |

---

## 14. Build & Run

### Manual Build & Run

```bash
cd ~/AOC/Lib/AI/AIVisionServerDealer
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Run tests
ctest --output-on-failure

# Run node
./ai_vision_server_dealer [config_dir] [cert_path] [key_path]
```

### Start Script

A management script is provided at `scripts/start.sh`.

```bash
./scripts/start.sh start              # Build (if needed) and start
./scripts/start.sh stop               # Stop (graceful SIGTERM)
./scripts/start.sh restart            # Stop and start
./scripts/start.sh status             # Check running status
./scripts/start.sh build              # Build only
```

---

## 15. Deployment

| Server | IP | Role | Path |
|--------|-----|------|------|
| EDGE01 | 100.85.117.73 | Deployment | `/home/user/ECIDS/AIVisionServerDealer` |
| EDGE02 | 100.69.131.6 | Deployment | `/home/user/ECIDS/AIVisionServerDealer` |

See [server.md](server.md) for full connection and Git sync details.

---

## Related Documents

| Document | Description |
|----------|-------------|
| [API.md](API.md) | Full API reference (API1a detection, API1b admin, API2 data server) |
| [frame.md](frame.md) | Frame specification for all API boundaries |
| [config.md](config.md) | Configuration file design (Mode, ZMQ transport selection, HWM, API1/API2 settings) |
| [parameter.md](parameter.md) | Admin parameter list |
| [folder_structure.md](folder_structure.md) | Project directory layout |
| [server.md](server.md) | Server connection details and Git sync |
