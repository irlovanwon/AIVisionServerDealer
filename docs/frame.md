# AIVisionServerDealer — Frame Specification

> This document defines the frame formats at every API boundary. For the commands that use these frames, see [API.md](API.md). For configuration values referenced in frames (`dealer_id`, transport, endpoints), see [config.md](config.md). Example payloads are in [API design/Examples/](API%20design/Examples/).

---

## Frame Flow Summary

```
  API2a (ZMQ SUB — multi-type data in)     API1a (ZMQ DEALER/DEALER — detection)         API2b (HTTPS — result out)

  Data Server publishes:                    Dealer → AI Client (request, 3 modes):
  ┌─────────────────────────────┐           ┌────────────────────────────────────┐       AI Client → Dealer (response):
  │ Visual & Geometric (2-part): │           │  • File   — single JSON (URI=path) │       ┌──────────────────────────────┐
  │   Part 1 (JSON header)       │           │  • Http   — single JSON (URI=url)  │       │  TransactionID               │
  │    type, ts, pair_id         │           │  • Binary — JSON + JPEG (default)  │       │  DealerID                    │
  │    channel                   │           │  TransactionID, Mode, DealerID     │       │  Result[] (detections)       │
  │   Part 2 (binary payload)    │           │  Data[]                            │       │  TimestampReceived           │
  │ Sensor & Tracking (JSON-only):│          │  Timestamp                         │       │  TimestampReplied            │
  │   JSON header (no binary)     │          └────────────────────────────────────┘       └──────────────────────────────┘
  │    type, ts, readings        │                                                          │
  └─────────────────────────────┘                                                          ▼
                                                                             Dealer → Data Server (HTTPS POST):
  API1b (HTTPS — admin)                                       ┌──────────────────────────────────┐
                                                              │  Request:  TransactionID, DealerID│
  Admin Client → Dealer (HTTPS POST):                         │   Action, Parameter[], Timestamp │
  ┌──────────────────────────────┐                            │  Response: Status, Parameter[]    │
  │  TransactionID               │                            │   TimestampReceived, TimestampReplied│
  │  DealerID                    │                            └──────────────────────────────────┘
  │  Action                      │
  │  Parameter[]                 │
  │  Timestamp                   │
  └──────────────────────────────┘
  Dealer → Admin Client:
  ┌──────────────────────────────┐
  │  TransactionID               │
  │  DealerID                    │
  │  Status                      │
  │  Parameter[]                 │
  │  TimestampReceived           │
  │  TimestampReplied            │
  └──────────────────────────────┘
```

---

## Timestamp Format

All timestamps follow `YYYYMMDD-HHMMSS-sss` per [Coding/rule.md §3](../../Coding/rule.md). The API design examples use the `YYYYMMDD-HHMMSS` form (second precision); millisecond precision (`-sss`) is appended where sub-second timing is needed.

---

## API 1a — AI Detection Frame (ZMQ DEALER/DEALER)

Bidirectional asynchronous frames between the Dealer and the AI client over ZMQ DEALER/DEALER. Supports three message formats depending on `Mode`:

- **Mode=`"File"`** — Single-part: one JSON document (image references by local file URI)
- **Mode=`"Http"`** — Single-part: one JSON document (image references by HTTP/HTTPS URL)
- **Mode=`"Binary"`** *(default)* — Two-part: Part 1 JSON metadata + Part 2 binary JPEG-encoded payload (inline image data)

> Connection details (protocol, transport, identity, HWM): see [API.md §API1a](API.md#api-1a--ai-detection-zmq-dealerdealer) and [config.md §API1a](config.md#api-1a--ai-detection-zmq-dealerdealer).

### Request Frame (Dealer → AI Client)

#### Mode = `"File"` (single-part)

> Source: [API design/Examples/Request-AI detection.json](API%20design/Examples/Request-AI%20detection.json)

Single ZMQ message part containing JSON. `Data[].URI` holds a local file path:

```json
{
    "TransactionID": "20260303012213",
    "Mode": "File",
    "DealerID": "Edge001",
    "Data": [
        {
            "URI": "/home/ecids/image/20260303-012213-226-L.jpg",
            "FileName": "RAW-20260303-012213-226-L.jpg",
            "Resolution": "1920,1080",
            "Format": "RGB"
        },
        {
            "URI": "/home/ecids/image/20260303-012213-226-R.jpg",
            "FileName": "RAW-20260303-012213-226-R.jpg",
            "Resolution": "1920,1080",
            "Format": "RGB"
        }
    ],
    "Timestamp": "20260423-111223"
}
```

#### Mode = `"Http"` (single-part)

Single ZMQ message part containing JSON. `Data[].URI` holds an HTTP/HTTPS URL that the AI client can fetch:

```json
{
    "TransactionID": "20260303012213",
    "Mode": "Http",
    "DealerID": "Edge001",
    "Data": [
        {
            "URI": "https://192.168.0.128:8090/image/123.jpg",
            "FileName": "img.jpg",
            "Resolution": "1920,1080",
            "Format": "RGB"
        }
    ],
    "Timestamp": "20260423-111223"
}
```

#### Mode = `"Binary"` (two-part — default)

Two ZMQ message parts sent as one multipart message. `Data[].URI` is empty (the image bytes travel inline in Part 2).

| Part | Content | Description |
|------|---------|-------------|
| Part 1 | JSON metadata | Same JSON structure as Mode=`"File"`, but `Data[].URI` is empty (image data is inline) |
| Part 2 | Binary payload | JPEG-encoded image bytes |

Part 1 (JSON metadata):

```json
{
    "TransactionID": "20260623-024923-555-2",
    "Mode": "Binary",
    "DealerID": "Edge001",
    "Data": [
        {
            "URI": "",
            "FileName": "IMG-20260623-024923-555-2.jpg",
            "Resolution": "1920,1080",
            "Format": "RGB"
        }
    ],
    "Timestamp": "20260623-024923"
}
```

Part 2 (binary): JPEG-encoded image payload (sent via `zmq_msg_init_data` zero-copy).

> **Zero-copy:** Both parts use `zmq_msg_init_data` with heap-allocated `std::string` (Part 1) and `PayloadPtr` (Part 2) to avoid data copying.

> **JPEG encoding:** In Binary mode, API2 ingest data is JPEG-encoded before being transferred to the AI client. The Dealer re-encodes incoming frames (image/depth/point-cloud) into JPEG bytes for Part 2. See [API 2a](#api-2a--data-ingest-frame-zmq-sub).

#### Field Reference

| Field | Type | Always Present | Source | Description |
|-------|------|----------------|--------|-------------|
| `TransactionID` | `string` | Yes | Dealer | Unique ID for this detection transaction. Used to correlate request and response. |
| `Mode` | `string` | Yes | Dealer | Detection mode: `"File"` (local path), `"Http"` (HTTP/HTTPS URL), or `"Binary"` (inline JPEG, default) |
| `DealerID` | `string` | Yes | Config | Dealer identifier from `config.default.json` `dealer_id`. See [config.md](config.md). |
| `Data` | `array` | Yes | Dealer | List of image entries to detect |
| `Data[].URI` | `string` | Mode-dependent | Dealer | Image location — see [URI behavior per mode](#uri-behavior-per-mode) below |
| `Data[].FileName` | `string` | Yes | Dealer | Image file name — used to correlate results in the response |
| `Data[].Resolution` | `string` | Yes | Dealer | Image resolution as `"width,height"` |
| `Data[].Format` | `string` | Yes | Dealer | Image format: `"Mono"`, `"Color"`, `"RGB"` |
| `Timestamp` | `string` | Yes | Dealer | Time the request was sent (`YYYYMMDD-HHMMSS`) |

##### URI behavior per mode

| Mode | `Data[].URI` value | Image data delivered via |
|------|--------------------|--------------------------|
| `"File"` | Local file path (e.g. `/home/ecids/image/img.jpg`) | File on disk — AI client reads the path |
| `"Http"` | HTTP/HTTPS URL (e.g. `https://192.168.0.128:8090/image/123.jpg`) | AI client fetches the URL |
| `"Binary"` | `""` (empty — data is inline) | Part 2 binary JPEG payload |

**Stereo camera:** When multiple images belong to the same frame (e.g., left + right from a stereo camera), both entries are included in the same `Data[]` array and grouped by timestamp. See [design.md §5.2](design.md#52-api-2--dealer--data-server).

### Response Frame (AI Client → Dealer)

> Source: [API design/Examples/Response-AI detection.json](API%20design/Examples/Response-AI%20detection.json)

```json
{
    "TransactionID": "20260303012213",
    "DealerID": "AIModule001",
    "Result": [
        {
            "LabelID": "OCR",
            "Confidence": "0.96",
            "FileName": "RAW-20260303-012213-226-L.jpg",
            "Coordinates": "x1,y1,x2,y2,x3,y3,x4,y4",
            "OCR": "2021222",
            "TimestampStart": "",
            "TimestampEnd": ""
        },
        {
            "LabelID": "T1",
            "Confidence": "0.96",
            "FileName": "RAW-20260303-012213-226-L.jpg",
            "Coordinates": "x1,y1,x2,y2,x3,y3,x4,y4",
            "TimestampStart": "",
            "TimestampEnd": ""
        },
        {
            "LabelID": "groove2",
            "Confidence": "0.96",
            "FileName": "RAW-20260303-012214-500-L.jpg",
            "Coordinates": "x1,y1,x2,y2,x3,y3,x4,y4",
            "TimestampStart": "",
            "TimestampEnd": ""
        }
    ],
    "TimestampReceived": "20260423-111223",
    "TimestampReplied": "20260423-111229"
}
```

| Field | Type | Always Present | Source | Description |
|-------|------|----------------|--------|-------------|
| `TransactionID` | `string` | Yes | AI Client | Echoes the request `TransactionID` for correlation |
| `DealerID` | `string` | Yes | AI Client | AI client/module identifier (e.g., `"AIModule001"`) |
| `Result` | `array` | Yes | AI Client | List of detection results |
| `Result[].LabelID` | `string` | Yes | AI Client | Detected label/class (e.g., `"OCR"`, `"T1"`, `"groove2"`) |
| `Result[].Confidence` | `string` | Yes | AI Client | Detection confidence score (`0.0`–`1.0`) |
| `Result[].FileName` | `string` | Yes | AI Client | Source image file name — correlates to request `Data[].FileName` |
| `Result[].Coordinates` | `string` | Yes | AI Client | Detection bounding polygon: 4 corner points `"x1,y1,x2,y2,x3,y3,x4,y4"` |
| `Result[].OCR` | `string` | No | AI Client | OCR text result (present only for `LabelID = "OCR"`) |
| `Result[].TimestampStart` | `string` | Yes | AI Client | Detection processing start timestamp (empty if not measured) |
| `Result[].TimestampEnd` | `string` | Yes | AI Client | Detection processing end timestamp (empty if not measured) |
| `TimestampReceived` | `string` | Yes | AI Client | When the AI client received the request |
| `TimestampReplied` | `string` | Yes | AI Client | When the AI client sent the response |

**Round-trip latency:** `TimestampReplied - TimestampReceived` = AI client processing time. End-to-end latency = `TimestampReplied - request Timestamp`.

---

## API 1b — Admin Frame (HTTPS)

JSON frames for the HTTPS admin interface. Each frame is a JSON document sent via `POST`.

> Connection details (host, port, TLS): see [API.md §API1b](API.md#api-1b--admin-command-https) and [config.md §API1b](config.md#api-1b--admin-https).

### Request Frame (Admin Client → Dealer)

> Source: [API design/Examples/Request-AI config.json](API%20design/Examples/Request-AI%20config.json)

```json
{
    "TransactionID": "20260303012213",
    "DealerID": "Edge001",
    "Action": "SetParameter",
    "Parameter": [
        {
            "ID": "Mode",
            "Value": "GPU"
        }
    ],
    "Timestamp": "20260423-111223"
}
```

| Field | Type | Always Present | Source | Description |
|-------|------|----------------|--------|-------------|
| `TransactionID` | `string` | Yes | Client | Unique ID for this admin transaction |
| `DealerID` | `string` | Yes | Client | Client identifier (e.g., `"Edge001"`) |
| `Action` | `string` | Yes | Client | Admin action: `"SetParameter"`, `"GetParameter"`, `"CheckStatus"`, `"Pause"`, `"Resume"`, `"AckResult"`, `"Reconnect"` |
| `Parameter` | `array` | Yes | Client | List of parameter entries. See [parameter.md](parameter.md). |
| `Parameter[].ID` | `string` | Yes | Client | Parameter identifier (e.g., `"Mode"`) |
| `Parameter[].Value` | `string` | Yes | Client | Parameter value (string-encoded) |
| `Timestamp` | `string` | Yes | Client | Time the request was sent |

### Response Frame (Dealer → Admin Client)

> Source: [API design/Examples/Response-AI config.json](API%20design/Examples/Response-AI%20config.json)

```json
{
    "TransactionID": "20260303012213",
    "DealerID": "AIModule001",
    "Status": "1",
    "Parameter": [
        {
            "ID": "Mode",
            "Value": "GPU"
        }
    ],
    "TimestampReceived": "20260423-111223",
    "TimestampReplied": "20260423-111229"
}
```

| Field | Type | Always Present | Source | Description |
|-------|------|----------------|--------|-------------|
| `TransactionID` | `string` | Yes | Dealer | Echoes the request `TransactionID` |
| `DealerID` | `string` | Yes | Dealer | Dealer identifier from `config.dealer_id` (e.g., `"Edge001"`) |
| `Status` | `string` | Yes | Dealer | `"1"` = success, `"0"` = failure |
| `Parameter` | `array` | Yes | Dealer | Echoed parameter list with applied/queried values |
| `Parameter[].ID` | `string` | Yes | Dealer | Parameter identifier |
| `Parameter[].Value` | `string` | Yes | Dealer | Parameter value |
| `TimestampReceived` | `string` | Yes | Dealer | When the Dealer received the request |
| `TimestampReplied` | `string` | Yes | Dealer | When the Dealer sent the response |

#### Flow Control Actions

The Dealer supports additional admin actions for pipeline flow control:

| Action | Parameters | Description |
|--------|------------|-------------|
| `Pause` | — | Pauses the ingest pipeline. New images are consumed from the buffer but not sent to the AI client. |
| `Resume` | — | Resumes the ingest pipeline from paused state. |
| `AckResult` | `Count` (int) | Acknowledges that `Count` published results have been received by the data server. If pending results drop below the limit, processing auto-resumes. |
| `CheckStatus` | — | Returns `"OK"` when running normally, `"Paused"` when processing is paused. |
| `Reconnect` | — | Re-establishes all API2a ZMQ SUB connections. Sent by the Data Server (Core) after it restarts, so the Dealer re-subscribes to all channels. See [design.md §12 Smart Handling](design.md#12-smart-handling). |

> **Backpressure mechanism:** The Dealer tracks pending (unacknowledged) results. When pending count reaches the limit (default: 10), the Dealer auto-pauses the pipeline. The data server should periodically call `AckResult` to acknowledge received results and allow processing to resume.

---

## API 2 — Data Server Frame

### API 2a — Data Ingest & Result Publish (ZMQ SUB + PUB)

API2a contains one ZMQ SUB (data ingest from data server) and one ZMQ PUB (AI result publish to data server).

The Dealer subscribes to multi-type data from the data server over ZMQ SUB. Two payload categories exist, distinguished by the header `type` field:

- **Visual & Geometric data** — 2-part ZMQ frame (JSON header + binary payload)
- **Sensor & Tracking data** — JSON-only (no binary part)

> Connection details (channels, transport, default channel = image only): see [API.md §API2a](API.md#api-2a--image-ingest-zmq-sub) and [config.md §API2a](config.md#api-2a--image-ingest-zmq-sub).

#### Part 1 — Header (JSON)

```json
{
    "type": "image",
    "channel": "image",
    "ts_sec": 1781020852,
    "ts_nsec": 888402198,
    "pair_id": 35653,
    "part": "left",
    "resolution": "1920,1080",
    "format": "Mono"
}
```

| Field | Type | Always Present | Description |
|-------|------|----------------|-------------|
| `type` | `string` | Yes | Data type — see [data types](#data-types) below |
| `channel` | `string` | Visual/Geo | Channel name: `"image"` (default) or `"stereo_image"` |
| `ts_sec` | `int64` | Yes | Capture timestamp — seconds |
| `ts_nsec` | `int64` | Yes | Capture timestamp — nanoseconds |
| `pair_id` | `uint64` | Yes | Groups data from the same frame (e.g., stereo left + right) |
| `part` | `string` | Stereo only | `"left"` or `"right"` — only for stereo camera multiple images |
| `resolution` | `string` | Visual/Geo | Image resolution `"width,height"` |
| `format` | `string` | Visual/Geo | Image format: `"Mono"`, `"Color"`, `"RGB"` |

#### Part 2 — Payload (binary, visual & geometric types only)

| `type` | Payload Format |
|---------|---------------|
| `image` | Single image (JPEG / raw) |
| `stereo_image` | One part per message; left and right share the same `pair_id` |
| `depth_map` | Depth map (raw float / 16-bit) |
| `point_cloud` | 3D point cloud (XYZ float buffer) |

> **JPEG encoding:** For visual & geometric types, the binary payload is JPEG-encoded before being forwarded to the AI client in [API1a Binary mode](#mode--binary-two-part--default).

#### Data Types

**Visual & Geometric data** (2-part: JSON header + binary payload):

| `type` | Description |
|--------|-------------|
| `image` | Image data (monocular camera frame) |
| `depth_map` | Depth map data (per-pixel distance) |
| `point_cloud` | 3D point cloud data (LiDAR / structured-light) |

**Sensor & Tracking data** (JSON-only, no binary part):

| `type` | Description |
|--------|-------------|
| `imu` | IMU data (accelerometer + gyroscope readings) |
| `magnetometer` | Magnetometer readings (compass heading / field vector) |
| `barometer` | Barometric pressure readings |

For JSON-only types, the full payload is carried in the Part 1 header (sensor readings as additional fields) and there is no Part 2.

> **Default subscription:** `image` only. For stereo cameras (`stereo_mode: true`), the `stereo_image` channel is also subscribed — multiple images (left + right) per frame are grouped by `pair_id`. See [config.md §API2a](config.md#api-2a--image-ingest-zmq-sub).

#### API 2a Result Publish Frame (ZMQ PUB)

The Dealer publishes AI detection results back to the data server via ZMQ PUB. The data server connects a ZMQ SUB to the `result_endpoint` (default: `ipc:///tmp/ai_vision_dealer_result`).

Each result is a 2-part ZMQ multipart message:

**Part 1 — Header (JSON):**

```json
{
    "ts_sec": 1781020852,
    "ts_nsec": 888402198,
    "transaction_id": "20260623-152538-349-2",
    "dealer_id": "Edge001"
}
```

**Part 2 — Result (JSON):**

```json
{
    "TransactionID": "20260623-152538-349-2",
    "DealerID": "Edge001",
    "Status": "1",
    "Result": [
        {
            "LabelID": "T1",
            "Confidence": "0.80",
            "FileName": "IMG-20260623-152538-349-2.jpg",
            "Coordinates": "100,100,200,100,200,200,100,200"
        }
    ],
    "TimestampReplied": "20260623-152538-989"
}
```

When no AI client is connected, a status result is published instead:

```json
{
    "TransactionID": "20260623-152524-831-1",
    "DealerID": "Edge001",
    "Status": "0",
    "Reason": "No AI client connected",
    "Result": [],
    "TimestampReplied": "20260623-152525-332"
}
```

> See [design.md §11 No-Dealer Handling](design.md#11-no-dealer-handling-api1a) for the no-client throttling mechanism.

### API 2b — Result Frame (HTTPS)

Detection results are returned to the data server over HTTPS `POST`. The frame format mirrors [API1b admin](#api-1b--admin-frame-https) — request/response JSON documents.

> Connection details (host, port, TLS): see [API.md §API2b](API.md#api-2b--result-https) and [config.md §API2b](config.md#api-2b--result-https).

#### Request Frame (Dealer → Data Server)

The Dealer POSTs the detection result to the data server. `Action = "PublishResult"`; the detection results from the AI client are carried in `Parameter[]`.

```json
{
    "TransactionID": "20260303012213",
    "DealerID": "Edge001",
    "Action": "PublishResult",
    "Parameter": [
        {
            "ID": "Result",
            "Value": "..."
        }
    ],
    "Timestamp": "20260423-111229"
}
```

| Field | Type | Always Present | Source | Description |
|-------|------|----------------|--------|-------------|
| `TransactionID` | `string` | Yes | Dealer | Original detection `TransactionID` for correlation |
| `DealerID` | `string` | Yes | Dealer | Dealer identifier from config |
| `Action` | `string` | Yes | Dealer | Result action: `"PublishResult"` |
| `Parameter` | `array` | Yes | Dealer | Result entries. Detection results are serialized here (see [Response Frame](#response-frame-ai-client--dealer)). |
| `Parameter[].ID` | `string` | Yes | Dealer | Entry identifier (e.g., `"Result"`) |
| `Parameter[].Value` | `string` | Yes | Dealer | Entry value (string-encoded) |
| `Timestamp` | `string` | Yes | Dealer | Time the request was sent |

**No-client status result (no AI client connected):** When no AI client is connected, the Dealer publishes a status result every 5 seconds (throttled):

```json
{
    "TransactionID": "20260614-141631-652-1",
    "DealerID": "Edge001",
    "Action": "PublishResult",
    "Parameter": [
        {
            "ID": "Status",
            "Value": "0"
        },
        {
            "ID": "Reason",
            "Value": "No AI client connected"
        }
    ],
    "Timestamp": "20260614-141632"
}
```

See [design.md §11 No-Dealer Handling](design.md#11-no-dealer-handling-api1a) for details.

#### Response Frame (Data Server → Dealer)

The data server acknowledges receipt of the result:

```json
{
    "TransactionID": "20260303012213",
    "DealerID": "Edge001",
    "Status": "1",
    "Parameter": [],
    "TimestampReceived": "20260423-111229",
    "TimestampReplied": "20260423-111230"
}
```

| Field | Type | Always Present | Source | Description |
|-------|------|----------------|--------|-------------|
| `TransactionID` | `string` | Yes | Data Server | Echoes the request `TransactionID` |
| `DealerID` | `string` | Yes | Data Server | Dealer identifier |
| `Status` | `string` | Yes | Data Server | `"1"` = success, `"0"` = failure |
| `Parameter` | `array` | Yes | Data Server | Echoed parameter list |
| `TimestampReceived` | `string` | Yes | Data Server | When the data server received the request |
| `TimestampReplied` | `string` | Yes | Data Server | When the data server sent the response |

---

## Related Documents

| Document | Description |
|----------|-------------|
| [API.md](API.md) | Full API command reference (uses these frame formats) |
| [config.md](config.md) | Configuration file design (`dealer_id`, transport, endpoints) |
| [parameter.md](parameter.md) | Admin parameter definitions |
| [design.md](design.md) | Architecture, data types, response codes |
| [API design/Examples/](API%20design/Examples/) | Example request/response JSON |
