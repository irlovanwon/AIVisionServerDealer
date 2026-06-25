# AIVisionServerDealer — API Reference

> Frame formats (request/response JSON, ZMQ message parts): see [frame.md](frame.md).  
> Configuration (endpoints, transport selection): see [config.md](config.md).  
> Example payloads: see [API design/Examples/](API%20design/Examples/).

---

## General Rules

### ZMQ Transport

Both `tcp` and `ipc` shall be supported for every ZMQ-based API module. The transport mode is selectable per module via configuration. See [config.md](config.md).

- Use `ipc://` for intra-machine communication (lowest latency)
- Use `tcp://` for inter-machine communication

### ZMQ DEALER/DEALER (API1a)

Per [Coding/ZMQ.md](../../Coding/ZMQ.md) DEALER/DEALER pattern:

- Both sides operate asynchronously — send and receive without blocking
- Use `zmq_poll` or async event loops to multiplex; never perform CPU-intensive work on the polling thread (offload to a ThreadPool)
- Configure `ZMQ_SNDHWM` and `ZMQ_RCVHWM` on both dealer sockets to bound queues; dropping old frames is preferred over blocking
- Set a unique `ZMQ_IDENTITY` on each dealer socket for message tracing and routing

### HTTPS Admin (API1b / API2b)

- TLS mandatory (OpenSSL)
- JSON request/response bodies

### Shared Pointers for Dataflow

Internal data transfer uses `std::shared_ptr` instead of memory copy. See [design.md §8](design.md#8-key-design-decisions).

### JPEG Encoding

All image data ingested via API2 is **JPEG-encoded** before being transferred to the AI client. This applies to every detection mode — the binary payload in Binary mode is JPEG, and the files/URLs referenced in File/Http mode are expected to be JPEG images.

---

## API 1 — Dealer ↔ AI Client

API 1 is used for the Dealer to communicate with AI client(s). It is split into two channels: **API1a** (AI detection, ZMQ) and **API1b** (admin, HTTPS).

### API 1a — AI Detection (ZMQ DEALER/DEALER)

Used for AI detection. The Dealer sends image data to the AI client; the AI client returns detection results. Supports **three detection modes**:

- **`Mode="File"`** — single-part JSON message where `Data[].URI` is a **local file path** on the AI client machine. The AI client reads the image from disk.
- **`Mode="Http"`** — single-part JSON message where `Data[].URI` is an **HTTP/HTTPS URL**. The AI client downloads the image from the URL.
- **`Mode="Binary"`** *(default)* — two-part ZMQ multipart message: Part 1 = JSON metadata, Part 2 = inline binary image data (JPEG-encoded, zero-copy via `zmq_msg_init_data`). No external fetch is required by the AI client.

| Property | Value |
|----------|-------|
| Role | AIVisionServerDealer = ZMQ DEALER; AI Client = ZMQ DEALER |
| Protocol | ZMQ DEALER/DEALER |
| Direction | Bidirectional (Dealer → AI Client request; AI Client → Dealer response) |
| Supported data type | Image |
| Transport | `tcp` or `ipc` (configurable via [config.md](config.md) `api1a.transport`) |
| Default mode | `Binary` |
| Asynchronous | Yes |

#### Request Frame — File Mode (Detection Request)

Single-part JSON message. `Data[].URI` points to a local file on the AI client machine.

See [frame.md §API1a Request](frame.md#request-frame-dealer--ai-client) and example [API design/Examples/Request-AI detection.json](API%20design/Examples/Request-AI%20detection.json).

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
            "Format": "Mono"
        },
        {
            "URI": "/home/ecids/image/20260303-012213-226-R.jpg",
            "FileName": "RAW-20260303-012213-226-R.jpg",
            "Resolution": "1920,1080",
            "Format": "Mono"
        }
    ],
    "Timestamp": "20260423-111223"
}
```

#### Request Frame — Http Mode (Detection Request)

Single-part JSON message. `Data[].URI` is an HTTP/HTTPS URL the AI client fetches.

```json
{
    "TransactionID": "20260303012213",
    "Mode": "Http",
    "DealerID": "Edge001",
    "Data": [
        {
            "URI": "https://data-server/images/20260303-012213-226-L.jpg",
            "FileName": "RAW-20260303-012213-226-L.jpg",
            "Resolution": "1920,1080",
            "Format": "Mono"
        },
        {
            "URI": "https://data-server/images/20260303-012213-226-R.jpg",
            "FileName": "RAW-20260303-012213-226-R.jpg",
            "Resolution": "1920,1080",
            "Format": "Mono"
        }
    ],
    "Timestamp": "20260423-111223"
}
```

#### Request Frame — Binary Mode (Detection Request, default)

Two-part ZMQ multipart message. Part 1 is the JSON metadata; Part 2 is the inline binary image data (JPEG-encoded, zero-copy via `zmq_msg_init_data`).

**Part 1 — JSON metadata:**

```json
{
    "TransactionID": "20260303012213",
    "Mode": "Binary",
    "DealerID": "Edge001",
    "Data": [
        {
            "URI": "",
            "FileName": "RAW-20260303-012213-226-L.jpg",
            "Resolution": "1920,1080",
            "Format": "Mono",
            "ImageIndex": 0
        }
    ],
    "Timestamp": "20260423-111223"
}
```

> For multiple images, additional binary parts follow (Part 2, Part 3, …) and each `Data[]` entry carries an `ImageIndex` referencing the corresponding binary part.

**Part 2 — binary image data (JPEG):**

```
<raw JPEG bytes, zero-copy via zmq_msg_init_data>
```

#### Response Frame (Detection Result)

See [frame.md §API1a Response](frame.md#response-frame-ai-client--dealer) and example [API design/Examples/Response-AI detection.json](API%20design/Examples/Response-AI%20detection.json).

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
        }
    ],
    "TimestampReceived": "20260423-111223",
    "TimestampReplied": "20260423-111229"
}
```

#### Thread Strategy

- A dedicated poll thread reads/writes the DEALER socket via `zmq_poll`.
- A `std::condition_variable` notifies the poll thread when new detection requests are enqueued from the Core pipeline; responses are dispatched back to the Core pipeline.
- For Binary mode, image data is sent zero-copy via `zmq_msg_init_data` — no memory copy.
- When `ZMQ_SNDHWM` is reached, the **drop-newest** policy is applied: the newest outgoing request is dropped rather than blocking the pipeline.
- CPU-intensive serialization is offloaded to a ThreadPool — never on the polling thread.

#### No-Dealer Handling

If no AI client is connected, the Dealer prevents data overflow via:

- **ZMQ socket monitor** — tracks connection state (`ZMQ_EVENT_CONNECTED` / `ZMQ_EVENT_DISCONNECTED`)
- **ZMQ_IMMEDIATE + ZMQ_DONTWAIT** — messages are never queued before a peer connects; send never blocks
- **Pipeline guard** — images are consumed from the buffer (keeping it healthy) but no detection request is sent and no file is written
- **Status result** — a status result is published via API2b (HTTPS) every 5 seconds:

```json
{
    "TransactionID": "...",
    "DealerID": "Edge001",
    "Status": "0",
    "Reason": "No AI client connected",
    "Result": [],
    "TimestampReplied": "..."
}
```

See [design.md §11](design.md#11-no-dealer-handling-api1a) for details.

---

### API 1b — Admin Command (HTTPS)

Used for admin commands — parameter get/set, mode selection, status. The Dealer exposes an HTTPS server.

| Property | Value |
|----------|-------|
| Role | AIVisionServerDealer = HTTPS Server |
| Protocol | HTTPS (TLS mandatory) |
| Direction | Admin Client → Dealer (request); Dealer → Admin Client (response) |
| Content-Type | `application/json` |
| Method | `POST` |

#### Request Frame

See [frame.md §API1b Request](frame.md#request-frame-admin-client--dealer) and example [API design/Examples/Request-AI config.json](API%20design/Examples/Request-AI%20config.json).

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

#### Response Frame

See [frame.md §API1b Response](frame.md#response-frame-dealer--admin-client) and example [API design/Examples/Response-AI config.json](API%20design/Examples/Response-AI%20config.json).

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

#### Supported Actions

| Action | Description |
|--------|-------------|
| `SetParameter` | Set one or more parameters on the AI client |
| `GetParameter` | Get one or more parameters from the AI client |
| `CheckStatus` | Check module status (returns `"OK"` or `"Paused"`) |
| `Pause` | Pause the ingest pipeline (images consumed but not forwarded) |
| `Resume` | Resume the ingest pipeline from paused state |
| `AckResult` | Acknowledge published results (`Count` parameter) — decrements pending counter, auto-resumes if below limit |

See [parameter.md](parameter.md) for the parameter list.

---

## API 2 — Dealer ↔ Data Server

API 2 is used for the Dealer to communicate with the data server. **API2a** provides data subscription (ZMQ SUB) and AI result publishing (ZMQ PUB). **API2b** provides command transfer (HTTPS).

### API 2a — Data Subscription & Result Publish (ZMQ SUB + ZMQ PUB)

API2a contains **one ZMQ SUB** and **one ZMQ PUB**:

**ZMQ SUB** — subscribes to data published by the data server. The subscription covers visual/geometric data, sensor/tracking data, and other JSON data.

**ZMQ PUB** — publishes AI detection results back to the data server. The data server subscribes to this PUB endpoint to receive results.

| Property | Value |
|----------|-------|
| SUB Role | AIVisionServerDealer = ZMQ Subscriber (SUB); Data Server = ZMQ Publisher (PUB) |
| PUB Role | AIVisionServerDealer = ZMQ Publisher (PUB); Data Server = ZMQ Subscriber (SUB) |
| Direction | Data Server → Dealer (SUB); Dealer → Data Server (PUB results) |
| Transport | `tcp` or `ipc` (selectable via command API — see [API2b](#api-2b--command-transfer-https)) |

#### Subscription Channels (SUB)

| Category | Channel | Content |
|----------|---------|---------|
| Visual & Geometric | `image` | Single image (mono or color) |
| Visual & Geometric | `stereo_image` | Paired left + right images |
| Visual & Geometric | `depth_map` | Depth map |
| Visual & Geometric | `point_cloud` | 3D point cloud data |
| Sensor & Tracking | `imu` | IMU data |
| Sensor & Tracking | `magnetometer` | Magnetometer data |
| Sensor & Tracking | `barometer` | Barometer data |
| Other | `json` | Generic JSON data |

The set of active channels is configurable via the command API (API2b). For stereo cameras, both `image` and `stereo_image` (left + right) channels are active — multiple images per frame are grouped by timestamp.

#### ZMQ Message Format

Each message is a 2-part ZMQ frame: JSON header (metadata) + binary payload (image/sensor data). All image data is JPEG-encoded before transfer to the AI client. See [frame.md §API2a](frame.md#api-2a--data-subscription-frame-zmq-sub).

#### Thread Strategy

- Dedicated subscriber threads (one per channel), each blocking on a `std::condition_variable` and waking on receive.
- Each callback pushes received data into the internal buffer (shared_ptr, no copy).
- For image data, binary is handled zero-copy via `zmq_msg_init_data`.
- When `ZMQ_RCVHWM` is reached, the **drop-newest** policy is applied: the newest incoming frame is dropped rather than blocking the subscriber.
- The Core pipeline consumes data from the buffers and packages detection requests for API1a.

#### Result Publishing (PUB)

The Dealer publishes AI detection results back to the data server via ZMQ PUB. The PUB endpoint is configurable via `api2a.result_endpoint` in the config (default: `ipc:///tmp/ai_vision_dealer_result`). The data server connects a ZMQ SUB to this endpoint to receive results.

Each result is a 2-part ZMQ multipart message:
- **Part 1** — Header JSON (timestamp, transaction_id, dealer_id)
- **Part 2** — Result JSON (detection results from the AI client, or status result when no AI client is connected)

See [frame.md §API2a Result](frame.md#api-2a--result-publish-frame-zmq-pub) for the frame format.

---

### API 2b — Command Transfer (HTTPS)

Used for command get/set between the Dealer and the data server. The Dealer exposes an HTTPS server (same style as API1b) that the **data server connects to** in order to get/set parameters — e.g. selecting the active subscription channels, transport (`tcp`/`ipc`), and other data-server configuration.

| Property | Value |
|----------|-------|
| Role | AIVisionServerDealer = HTTPS Server |
| Protocol | HTTPS (TLS mandatory) |
| Direction | Data Server → Dealer (request); Dealer → Data Server (response) |
| Content-Type | `application/json` |
| Method | `POST` |
| Frame format | Same as [API1b](#api-1b--admin-command-https) admin frame |

#### Request Frame

Uses the same frame format as API1b. See [API1b Request Frame](#request-frame).

```json
{
    "TransactionID": "20260303012213",
    "DealerID": "Edge001",
    "Action": "SetParameter",
    "Parameter": [
        {
            "ID": "api2a.transport",
            "Value": "ipc"
        }
    ],
    "Timestamp": "20260423-111223"
}
```

#### Response Frame

Uses the same frame format as API1b. See [API1b Response Frame](#response-frame).

```json
{
    "TransactionID": "20260303012213",
    "DealerID": "AIModule001",
    "Status": "1",
    "Parameter": [
        {
            "ID": "api2a.transport",
            "Value": "ipc"
        }
    ],
    "TimestampReceived": "20260423-111223",
    "TimestampReplied": "20260423-111229"
}
```

#### Supported Configuration

Find supported configuration from the **AI config part** in [API design](API%20design/). Supported parameters include subscription channel selection, `tcp`/`ipc` transport selection for API2a, and HWM/buffer tuning.

#### Reconnect Action (Core Restart)

When the Data Server (Core) restarts, it must notify the Dealer to re-establish its ZMQ SUB connections:

```json
{
    "TransactionID": "20260625-080000-000-1",
    "DealerID": "Edge001",
    "Action": "Reconnect",
    "Parameter": [],
    "Timestamp": "20260625-080000"
}
```

The Dealer closes all SUB sockets, re-creates them, and re-connects to all configured channels. See [design.md §12 Smart Handling](design.md#12-smart-handling).

---

## End-to-End Data Flow

```
1. Data Server publishes data (API2a ZMQ PUB) — image, depth map, point cloud,
   IMU, magnetometer, barometer, json
2. Dealer ZMQ SUB receives data → internal buffers (shared_ptr, zero-copy)
3. Core packages detection request (TransactionID + Data[])
4. Dealer ZMQ DEALER sends request to AI Client (API1a)
5. AI Client processes → returns detection result (API1a)
6. Core maps result to original TransactionID
7. Dealer ZMQ PUB publishes result to Data Server (API2a PUB)
```

Admin commands run independently of the detection pipeline:

- **API1b HTTPS** — admin client configures the AI client (mode, parameters, status).
- **API2b HTTPS** — data server gets/sets data-server-side parameters (channels, transport).

---

## Related Documents

| Document | Description |
|----------|-------------|
| [frame.md](frame.md) | Frame specification for all API boundaries |
| [config.md](config.md) | Configuration (ZMQ transport selection, endpoints) |
| [parameter.md](parameter.md) | Admin parameter list |
| [design.md](design.md) | System architecture, design decisions, constraints |
| [API design/Examples/](API%20design/Examples/) | Example request/response JSON |
