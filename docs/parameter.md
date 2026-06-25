# AIVisionServerDealer — Parameters

> Parameters are read/written via the admin API1b commands `SetParameter` / `GetParameter`. See [API.md §API1b](API.md#api-1b--admin-command-https) and [frame.md §API1b](frame.md#api-1b--admin-frame-https).

---

## Introduction

Admin parameters configure the AI client's inference behavior and processing mode. They are forwarded to the AI client via the API1b HTTPS admin channel.

## Design Principles

- Each parameter has a unique `ID` and a `Value`.
- Parameters are set via the `SetParameter` action and read via the `GetParameter` action in the API1b admin frame.
- The `Status` field in the response confirms whether the parameter was applied successfully (`1` = success).

---

## Parameter Model

Each parameter in the admin frame uses the following structure:

```json
{
    "ID": "Mode",
    "Value": "GPU"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `ID` | `string` | Unique parameter identifier |
| `Value` | `string` | Parameter value (string-encoded) |

---

## All Parameters

### `Mode` — Data Transfer Mode

| Property | Value |
|----------|-------|
| ID | `Mode` |
| Type | Enum |
| Access | Read/Write |
| Description | Controls how image data is transferred from the Dealer to the AI client via API1a |

| Field | Value |
|-------|-------|
| Default | `Binary` |
| Options | `Binary`, `File`, `Http` |

**Notes:**
- `Binary` — Raw image data sent inline via ZMQ multipart messages `[JSON header][binary payload]`. Lowest latency, no disk/network dependency. The AI server decodes the binary frame and processes it directly.
- `File` — Image saved to local disk (`/tmp/ai_vision_images/`), file path sent as URI in the JSON request. The AI server reads the file from disk.
- `Http` — Image saved to local disk and served by the built-in HTTP file server (port from `http_file_server_port` config, default 8089). HTTP URL sent as URI in the JSON request. The AI server fetches the image via HTTP.
- Mode can be changed at runtime via `SetParameter` — takes effect on the next image.
- See [design.md §5.3](design.md#data-transfer--encoding-rules) for encoding details.

---

### `ConfidenceThreshold` — Detection Confidence Floor

| Property | Value |
|----------|-------|
| ID | `ConfidenceThreshold` |
| Type | Float |
| Access | Read/Write |
| Description | Minimum confidence for a detection result to be returned |

| Field | Value |
|-------|-------|
| Default | `0.5` |
| Min | `0.0` |
| Max | `1.0` |

**Notes:**
- Detections with confidence below this threshold are filtered out of the result set.

---

## Parameter Flow

```
Admin Client --(HTTPS SetParameter)--> AIVisionServerDealer (applies locally)
```

The `Mode` parameter is applied **locally** within the Dealer — it controls how the Dealer formats and sends detection requests to the AI client. Other parameters (e.g., `ConfidenceThreshold`) may be forwarded to the AI client depending on implementation.

See [frame.md §API1b](frame.md#api-1b--admin-frame-https) for the full request/response frame format.

---

## Parameter Response Format

### SetParameter Response

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

| Field | Type | Description |
|-------|------|-------------|
| `Status` | `string` | `"1"` = success, `"0"` = failure |
| `Parameter` | `array` | Echoed parameter list with applied values |
| `TimestampReceived` | `string` | When the Dealer received the request |
| `TimestampReplied` | `string` | When the Dealer/AI client replied |

---

## Flow Control Actions

In addition to parameters, the admin API1b supports flow control actions that manage the ingest pipeline:

### `Pause` — Pause Pipeline

| Property | Value |
|----------|-------|
| Action | `Pause` |
| Parameters | — |
| Description | Pauses the ingest pipeline. Images continue to be consumed from the buffer (preventing overflow) but are not forwarded to the AI client. |

### `Resume` — Resume Pipeline

| Property | Value |
|----------|-------|
| Action | `Resume` |
| Parameters | — |
| Description | Resumes the ingest pipeline from a paused state. |

### `AckResult` — Acknowledge Results

| Property | Value |
|----------|-------|
| Action | `AckResult` |
| Parameters | `Count` (int) — number of results to acknowledge |
| Description | Acknowledges that published results have been received by the data server. Decrements the pending result counter. If pending count drops below the limit (default: 10), processing auto-resumes. |

### `CheckStatus` — Query Status

| Property | Value |
|----------|-------|
| Action | `CheckStatus` |
| Parameters | — |
| Description | Returns `"OK"` when running normally, `"Paused"` when processing is paused. |

> **Backpressure flow:** When unacknowledged results reach the limit, the Dealer auto-pauses. The data server should call `AckResult` periodically to acknowledge consumed results.

---

## Related Documents

| Document | Description |
|----------|-------------|
| [API.md](API.md) | Admin API (API1b) command reference |
| [frame.md](frame.md) | Request/response frame formats |
| [config.md](config.md) | Configuration (`dealer_id`, transport settings) |
| [design.md](design.md) | Architecture, response codes |
