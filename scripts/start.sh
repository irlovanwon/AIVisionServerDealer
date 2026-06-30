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
APP_NAME="ai_vision_server_dealer"

kill_all_instances() {
    local pids
    pids=$(pgrep -f "${APP_NAME}" 2>/dev/null)
    if [ -n "$pids" ]; then
        echo "[INFO] Stopping all AIVisionServerDealer instances: $pids"
        for pid in $pids; do
            kill -TERM "$pid" 2>/dev/null || true
        done
        local count=0
        while pgrep -f "${APP_NAME}" > /dev/null 2>&1; do
            sleep 0.5
            count=$((count + 1))
            if [ $count -ge 20 ]; then
                echo "[INFO] Force-killing survivors..."
                pids=$(pgrep -f "${APP_NAME}" 2>/dev/null)
                for pid in $pids; do
                    kill -9 "$pid" 2>/dev/null || true
                done
                break
            fi
        done
        echo "[INFO] All AIVisionServerDealer instances stopped."
    fi
    rm -f "$PID_FILE"
}

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
    # Kill any existing instances first (prevents duplicates from manual starts)
    kill_all_instances
    sleep 0.5

    if [ ! -f "$BIN" ]; then
        build
    fi
    generate_certs

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
    kill_all_instances
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
