# AIVisionServerDealer

Bridges a **data server** and **AI client(s)** to handle vision data. Subscribes to image data from the data server, forwards it to the AI client for inference via ZMQ DEALER/DEALER, and publishes detection results back. Admin commands via HTTPS.

## Build

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

## Run

```bash
./scripts/start.sh start    # Build + generate certs + start
./scripts/start.sh stop
./scripts/start.sh status
```

## Tests

```bash
cd build && ctest --output-on-failure
```

## Configuration

See `config/default.json`. ZMQ transport (`tcp`/`ipc`) is configurable per API module.

## Architecture

- **API1a** — AI detection (ZMQ DEALER/DEALER)
- **API1b** — Admin commands (HTTPS)
- **API2a** — Image ingest (ZMQ SUB)
- **API2b** — Result publish (ZMQ PUB)
