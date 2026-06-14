#!/bin/bash
# AIVisionServerDealer management script

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
BIN="$BUILD_DIR/ai_vision_server_dealer"
CERT_DIR="$PROJECT_DIR/certs"
CERT="$CERT_DIR/server.crt"
KEY="$CERT_DIR/server.key"
PID_FILE="$PROJECT_DIR/ai_vision_server_dealer.pid"
LOG_FILE="$PROJECT_DIR/ai_vision_server_dealer.log"
CONFIG_FILE="$PROJECT_DIR/config/default.json"

build() {
    echo "[INFO] Building AIVisionServerDealer..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. > /dev/null 2>&1
    make -j$(nproc)
    if [ $? -ne 0 ]; then
        echo "[ERROR] Build failed"
        exit 1
    fi
    echo "[INFO] Build successful"
}

generate_certs() {
    if [ ! -f "$CERT" ] || [ ! -f "$KEY" ]; then
        echo "[INFO] Generating TLS certificates..."
        mkdir -p "$CERT_DIR"
        openssl req -x509 -newkey rsa:2048 -keyout "$KEY" -out "$CERT" \
            -days 365 -nodes -subj "/CN=ai-vision-server-dealer" 2>/dev/null
        echo "[INFO] Certificates generated at $CERT_DIR"
    fi
}

start() {
    if [ ! -f "$BIN" ]; then
        build
    fi
    generate_certs

    if [ -f "$PID_FILE" ] && kill -0 $(cat "$PID_FILE") 2>/dev/null; then
        echo "[ERROR] AIVisionServerDealer is already running (PID $(cat $PID_FILE))"
        exit 1
    fi

    echo "[INFO] Starting AIVisionServerDealer"
    echo "  Config: $CONFIG_FILE"
    echo "  Log:    $LOG_FILE"

    cd "$PROJECT_DIR"
    "$BIN" --config "$CONFIG_FILE" > "$LOG_FILE" 2>&1 &
    echo $! > "$PID_FILE"
    sleep 1

    if kill -0 $(cat "$PID_FILE") 2>/dev/null; then
        echo "[INFO] AIVisionServerDealer started (PID $(cat $PID_FILE))"
    else
        echo "[ERROR] AIVisionServerDealer failed to start. Check $LOG_FILE"
        rm -f "$PID_FILE"
        exit 1
    fi
}

stop() {
    if [ ! -f "$PID_FILE" ]; then
        echo "[WARN] PID file not found. Is AIVisionServerDealer running?"
        exit 0
    fi
    PID=$(cat "$PID_FILE")
    if kill -0 "$PID" 2>/dev/null; then
        echo "[INFO] Stopping AIVisionServerDealer (PID $PID)..."
        kill "$PID"
        sleep 2
        if kill -0 "$PID" 2>/dev/null; then
            kill -9 "$PID"
        fi
        echo "[INFO] AIVisionServerDealer stopped"
    else
        echo "[WARN] Process $PID not running"
    fi
    rm -f "$PID_FILE"
}

status() {
    if [ -f "$PID_FILE" ] && kill -0 $(cat "$PID_FILE") 2>/dev/null; then
        echo "[INFO] AIVisionServerDealer is running (PID $(cat $PID_FILE))"
    else
        echo "[INFO] AIVisionServerDealer is not running"
    fi
}

case "${1:-start}" in
    start)   start ;;
    stop)    stop ;;
    restart) stop; sleep 1; start ;;
    status)  status ;;
    build)   build ;;
    *)       echo "Usage: $0 {start|stop|restart|status|build}"; exit 1 ;;
esac
