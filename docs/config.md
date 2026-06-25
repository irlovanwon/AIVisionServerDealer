# AIVisionServerDealer — Configuration

> Configuration follows the standard JSON format as defined in [Coding/config_rule.md](../../Coding/config_rule.md).  
> Values referenced in frame formats are documented in [frame.md](frame.md).

---

## Configuration File

| Property | Value |
|----------|-------|
| Format | JSON |
| File | `config/default.json` |
| Loaded At | Module startup |
| Runtime Update | Via `SetParameter` command (API1b admin) |

---

## Configuration Strategy

### ZMQ Transport Selection (tcp / ipc)

ZMQ-based API modules support independent selection of `tcp` or `ipc` transport. This allows flexible deployment:

- **`ipc`** — intra-machine communication (lowest latency, used when the Dealer and the peer run on the same host)
- **`tcp`** — inter-machine communication (used when peers are on different hosts)

The transport is selected per module via the `transport` field in each API section:

| Module | Config Key | Default Transport |
|--------|-----------|-------------------|
| API1a (AI detection, DEALER/DEALER) | `api1a.transport` | `ipc` |
| API2a (data ingest, SUB) | `api2a.transport` | `ipc` |

API1b (admin) and API2b (result publish) always use HTTPS — no ZMQ transport selection.

### ZMQ High-Water Marks (HWM)

All ZMQ modules expose bounded queues via `sndhwm` (send) and/or `rcvhwm` (receive). When a queue reaches its limit, messages are dropped rather than blocking the pipeline (drop-newest policy).

| Module | Config Keys | Description |
|--------|------------|-------------|
| API1a (DEALER) | `sndhwm`, `rcvhwm` | Bounds both send and receive queues |
| API2a (SUB) | `rcvhwm` | Bounds the receive queue |

> API2b uses HTTPS — no ZMQ HWM applies.

---

## Config Structure

```json
{
    "version": "1.0",
    "dealer_id": "Edge001",
    "mode": "Binary",

    "api1a": {
        "transport": "ipc",
        "endpoint_local": "ipc:///tmp/ai_vision_dealer_detection",
        "endpoint_remote": "tcp://localhost:5555",
        "identity": "ai_vision_server_dealer",
        "sndhwm": 10,
        "rcvhwm": 10,
        "poll_timeout_ms": 100
    },

    "api1b": {
        "host": "0.0.0.0",
        "port": 8445,
        "cert_path": "certs/server.crt",
        "key_path": "certs/server.key",
        "worker_threads": 4
    },

    "api2a": {
        "transport": "ipc",
        "channels": {
            "image": "ipc:///tmp/data_server_image",
            "depth_map": "ipc:///tmp/data_server_depth",
            "point_cloud": "ipc:///tmp/data_server_pointcloud",
            "imu": "ipc:///tmp/data_server_imu",
            "magnetometer": "ipc:///tmp/data_server_mag",
            "barometer": "ipc:///tmp/data_server_baro"
        },
        "default_channel": "image",
        "rcvhwm": 10,
        "result_endpoint": "ipc:///tmp/ai_vision_dealer_result"
    },

    "api2b": {
        "host": "0.0.0.0",
        "port": 8446,
        "cert_path": "certs/server.crt",
        "key_path": "certs/server.key",
        "worker_threads": 4
    },

    "jpeg_quality": 85,
    "http_file_server_port": 8089,
    "log_level": "info"
}
```

---

## Configuration Sections

### Identity

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `dealer_id` | `string` | `"Edge001"` | Identifier for this AIVisionServerDealer instance. Included in detection request frames as `DealerID`. See [frame.md](frame.md). |

### Detection Mode

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `mode` | `string` | `"Binary"` | Detection mode controlling how requests are sent to the AI client via API1a: `"File"` (local file path), `"Http"` (HTTP/HTTPS URL), or `"Binary"` (inline JPEG bytes, zero-copy). See [API.md §API1a](API.md#api-1a--ai-detection-zmq-dealerdealer). |

### API 1a — AI Detection (ZMQ DEALER/DEALER)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `api1a.transport` | `string` | `"ipc"` | ZMQ transport: `"ipc"` or `"tcp"` |
| `api1a.endpoint_local` | `string` | `"ipc:///tmp/ai_vision_dealer_detection"` | Endpoint used when `transport = "ipc"` |
| `api1a.endpoint_remote` | `string` | `"tcp://localhost:5555"` | Endpoint used when `transport = "tcp"` (DEALER connects to AI client) |
| `api1a.identity` | `string` | `"ai_vision_server_dealer"` | `ZMQ_IDENTITY` for message tracing. See [Coding/ZMQ.md](../../Coding/ZMQ.md) Identity Management. |
| `api1a.sndhwm` | `int` | `10` | Send high-water mark (bounded send queue) |
| `api1a.rcvhwm` | `int` | `10` | Receive high-water mark (bounded receive queue) |
| `api1a.poll_timeout_ms` | `int` | `100` | `zmq_poll` timeout in milliseconds |

### API 1b — Admin (HTTPS)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `api1b.host` | `string` | `"0.0.0.0"` | HTTPS server bind address |
| `api1b.port` | `int` | `8445` | HTTPS server listen port |
| `api1b.cert_path` | `string` | `"certs/server.crt"` | TLS certificate path |
| `api1b.key_path` | `string` | `"certs/server.key"` | TLS private key path |
| `api1b.worker_threads` | `int` | `4` | Number of HTTPS worker threads |

### API 2a — Data Subscription & Result Publish (ZMQ SUB + PUB)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `api2a.transport` | `string` | `"ipc"` | ZMQ transport: `"ipc"` or `"tcp"` |
| `api2a.channels` | `object` | (see below) | Per-channel ZMQ endpoint map — each data type has its own channel |
| `api2a.channels.image` | `string` | `"ipc:///tmp/data_server_image"` | Image channel endpoint |
| `api2a.channels.depth_map` | `string` | `"ipc:///tmp/data_server_depth"` | Depth map channel endpoint |
| `api2a.channels.point_cloud` | `string` | `"ipc:///tmp/data_server_pointcloud"` | Point cloud channel endpoint |
| `api2a.channels.imu` | `string` | `"ipc:///tmp/data_server_imu"` | IMU channel endpoint |
| `api2a.channels.magnetometer` | `string` | `"ipc:///tmp/data_server_mag"` | Magnetometer channel endpoint |
| `api2a.channels.barometer` | `string` | `"ipc:///tmp/data_server_baro"` | Barometer channel endpoint |
| `api2a.default_channel` | `string` | `"image"` | Default subscription channel |
| `api2a.rcvhwm` | `int` | `10` | Receive high-water mark (SUB side) |
| `api2a.result_endpoint` | `string` | `"ipc:///tmp/ai_vision_dealer_result"` | ZMQ PUB endpoint for publishing AI results back to the data server |

> **Multi-channel subscription:** The Dealer subscribes to one or more channels by name. Each data type (image, depth map, point cloud, IMU, magnetometer, barometer) is delivered on its own dedicated channel. The `default_channel` specifies which channel is subscribed at startup. See [API.md §API2a](API.md#api-2a--data-ingest-zmq-sub).

### API 2b — Result Publish (HTTPS Server)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `api2b.host` | `string` | `"0.0.0.0"` | HTTPS server bind address |
| `api2b.port` | `int` | `8446` | HTTPS server listen port (separate from API1b port 8445) |
| `api2b.cert_path` | `string` | `"certs/server.crt"` | TLS certificate path |
| `api2b.key_path` | `string` | `"certs/server.key"` | TLS private key path |
| `api2b.worker_threads` | `int` | `4` | Number of HTTPS worker threads |

> API2b is an HTTPS server — same structure as API1b but on a separate port. No ZMQ transport or HWM applies.

### Image Encoding

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `jpeg_quality` | `int` | `85` | JPEG encoding quality (1–100) applied to image data before transfer via API2. Higher values increase fidelity and bandwidth. |

### HTTP File Server (Http Mode)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `http_file_server_port` | `int` | `8089` | Port for the built-in HTTP file server. Used when `Mode = "Http"` — the Dealer saves images to `/tmp/ai_vision_images/` and serves them via this HTTP port. The AI client fetches images from `http://<host>:<port>/<filename>`. |

### Logging

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `log_level` | `string` | `"info"` | Log level: `"debug"`, `"info"`, `"warn"`, `"error"` |

---

## Transport Switching

For ZMQ-based modules (API1a, API2a), when `transport` is set to `"ipc"`, the module uses the `*_local` endpoint (or `channels.*` for API2a). When set to `"tcp"`, it uses the `*_remote` endpoint.

```
transport = "ipc"  →  endpoint_local  (ipc:///tmp/...)
transport = "tcp"  →  endpoint_remote (tcp://*:port)
```

For API2a, all channels in the `channels` map use the same transport mode. When switching, each channel endpoint must resolve to the corresponding transport scheme.

API2b uses HTTPS — no ZMQ transport switching applies.

---

## Runtime Updates

AI client inference parameters (e.g., `Mode`, `ConfidenceThreshold`) can be updated at runtime via `SetParameter` admin commands on API1b. These are forwarded to the AI client. See [parameter.md](parameter.md) for parameter definitions.

Dealer configuration values (transport, endpoints, ports) are **static** — they require a restart to take effect.

---

## Related Documents

| Document | Description |
|----------|-------------|
| [frame.md](frame.md) | Frame formats showing where `dealer_id` appears |
| [API.md](API.md) | Full API reference (API1a/1b/2) |
| [parameter.md](parameter.md) | Admin parameter list |
| [design.md](design.md) | Architecture, constraints (C6: tcp/ipc selectable) |
| [folder_structure.md](folder_structure.md) | Project directory layout including `config/` |
