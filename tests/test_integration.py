#!/usr/bin/env python3
"""
End-to-end integration test for AIVisionServerDealer.

Simulates all external components:
  - Mock Data Server (ZMQ PUB)  → publishes image to API2a
  - Mock AI Client  (ZMQ DEALER bind) → receives detection request, replies with results
  - Result Subscriber (ZMQ SUB) → receives published results from API2b
  - Admin Client (HTTPS) → sends SetParameter command to API1b

Test phases:
  1. No-client: no AI client → expect Status=0 status result
  2. Normal: with AI client → expect Status=1 detection result
  3. Admin: HTTPS SetParameter
"""

import zmq
import ssl
import json
import time
import http.client
import threading
import sys
import os

DETECTION_ENDPOINT = "ipc:///tmp/ai_vision_dealer_detection"
IMAGE_ENDPOINT     = "ipc:///tmp/data_server_image"
RESULT_ENDPOINT    = "ipc:///tmp/ai_vision_dealer_result"
ADMIN_HOST         = "127.0.0.1"
ADMIN_PORT         = 8445

results_received = []
results_lock = threading.Lock()


def mock_ai_client(ctx, ready_event, stop_event):
    """Mock AI client: DEALER bind, receive detection request, reply with results."""
    sock = ctx.socket(zmq.DEALER)
    sock.setsockopt(zmq.LINGER, 0)
    sock.bind(DETECTION_ENDPOINT)
    ready_event.set()
    print("[AI Client] DEALER bound at", DETECTION_ENDPOINT)

    poller = zmq.Poller()
    poller.register(sock, zmq.POLLIN)

    while not stop_event.is_set():
        events = dict(poller.poll(500))
        if sock in events:
            frames = sock.recv_multipart()
            msg = json.loads(frames[0].decode())
            binary_size = len(frames[1]) if len(frames) > 1 else 0
            print("[AI Client] Received detection request:")
            print("             TransactionID:", msg.get("TransactionID"))
            print("             DealerID:", msg.get("DealerID"))
            print("             Mode:", msg.get("Mode"))
            print("             Images:", len(msg.get("Data", [])))
            print("             Binary payload:", binary_size, "bytes")
            for d in msg.get("Data", []):
                print("               -", d.get("FileName"), d.get("Resolution"), d.get("Format"))

            response = {
                "TransactionID": msg.get("TransactionID"),
                "DealerID": "AIModule001",
                "Result": [
                    {
                        "LabelID": "OCR",
                        "Confidence": "0.96",
                        "FileName": msg["Data"][0]["FileName"] if msg.get("Data") else "",
                        "Coordinates": "100,100,200,100,200,200,100,200",
                        "OCR": "TEST123",
                        "TimestampStart": "",
                        "TimestampEnd": ""
                    },
                    {
                        "LabelID": "T1",
                        "Confidence": "0.94",
                        "FileName": msg["Data"][0]["FileName"] if msg.get("Data") else "",
                        "Coordinates": "50,50,150,50,150,150,50,150",
                        "TimestampStart": "",
                        "TimestampEnd": ""
                    }
                ],
                "TimestampReceived": msg.get("Timestamp", ""),
                "TimestampReplied": time.strftime("%Y%m%d-%H%M%S", time.localtime())
            }
            sock.send_json(response)
            print("[AI Client] Sent detection response (2 results)")

    sock.close()
    print("[AI Client] Stopped")


def result_subscriber(ctx, ready_event, stop_event):
    """Mock result subscriber: SUB connect, receive published results from API2b."""
    sock = ctx.socket(zmq.SUB)
    sock.setsockopt(zmq.SUBSCRIBE, b"")
    sock.setsockopt(zmq.LINGER, 0)
    sock.connect(RESULT_ENDPOINT)
    ready_event.set()
    print("[Result Sub] SUB connected to", RESULT_ENDPOINT)

    poller = zmq.Poller()
    poller.register(sock, zmq.POLLIN)

    while not stop_event.is_set():
        events = dict(poller.poll(500))
        if sock in events:
            header = json.loads(sock.recv_string())
            result = json.loads(sock.recv_string())
            status = result.get("Status", "?")
            reason = result.get("Reason", "")
            detections = result.get("Result", [])
            if reason:
                print(f"[Result Sub] Received status result:")
                print(f"             Status: {status}, Reason: {reason}")
            else:
                print("[Result Sub] Received published result:")
                print("             transaction_id:", header.get("transaction_id"))
                print("             dealer_id:", header.get("dealer_id"))
                print("             detections:", len(detections))
                for det in detections:
                    print("               -", det.get("LabelID"),
                          "conf=" + det.get("Confidence", ""),
                          "ocr=" + det.get("OCR", "") if det.get("OCR") else "")
            with results_lock:
                results_received.append(result)

    sock.close()
    print("[Result Sub] Stopped")


def publish_test_image(ctx):
    """Publish a single test image via ZMQ PUB."""
    sock = ctx.socket(zmq.PUB)
    sock.setsockopt(zmq.LINGER, 0)
    sock.bind(IMAGE_ENDPOINT)
    print("[Data Server] PUB bound at", IMAGE_ENDPOINT)

    time.sleep(1.0)

    header = {
        "channel": "image",
        "ts_sec": int(time.time()),
        "ts_nsec": 0,
        "pair_id": 1,
        "resolution": "1920,1080",
        "format": "Mono"
    }
    payload = b"\xff\xd8\xff\xe0" + b"\x00" * 100 + b"FAKE_JPEG_IMAGE_DATA"

    sock.send_json(header, zmq.SNDMORE)
    sock.send(payload)
    print("[Data Server] Published 1 image ({} bytes payload)".format(len(payload)))

    time.sleep(0.5)
    sock.close()
    print("[Data Server] Stopped")


def test_admin():
    """Send an admin SetParameter command via HTTPS."""
    request_body = {
        "TransactionID": "TEST-ADMIN-001",
        "DealerID": "Edge001",
        "Action": "SetParameter",
        "Parameter": [
            {"ID": "Mode", "Value": "GPU"},
            {"ID": "ConfidenceThreshold", "Value": "0.5"}
        ],
        "Timestamp": time.strftime("%Y%m%d-%H%M%S", time.localtime())
    }

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    conn = http.client.HTTPSConnection(ADMIN_HOST, ADMIN_PORT, context=ctx, timeout=5)
    body = json.dumps(request_body)
    conn.request("POST", "/", body, {"Content-Type": "application/json"})
    resp = conn.getresponse()
    data = resp.read().decode()
    conn.close()

    print("[Admin Client] Sent SetParameter (Mode=GPU, ConfidenceThreshold=0.5)")
    print("[Admin Client] HTTP status:", resp.status)
    resp_json = json.loads(data)
    print("[Admin Client] Response:")
    print("             Status:", resp_json.get("Status"))
    print("             DealerID:", resp_json.get("DealerID"))
    print("             Parameters:", resp_json.get("Parameter"))
    print("             TimestampReplied:", resp_json.get("TimestampReplied"))

    return resp_json.get("Status") == "1"


def main():
    print("=" * 60)
    print("AIVisionServerDealer — End-to-End Integration Test")
    print("=" * 60)
    print()

    ctx = zmq.Context()

    sub_ready = threading.Event()
    stop_event = threading.Event()

    # Start result subscriber first
    print("--- Starting result subscriber ---")
    sub_thread = threading.Thread(target=result_subscriber, args=(ctx, sub_ready, stop_event))
    sub_thread.start()
    sub_ready.wait(timeout=3)
    time.sleep(0.5)

    # --- Phase 1: No AI client connected ---
    print()
    print("--- Phase 1: No AI client connected ---")
    print("--- Publishing test image (no AI client) ---")
    publish_test_image(ctx)

    print("--- Waiting for no-client status result (max 10s) ---")
    deadline = time.time() + 10
    no_client_result = None
    while time.time() < deadline:
        with results_lock:
            if results_received:
                no_client_result = results_received[-1]
                if no_client_result.get("Status") == "0":
                    break
        time.sleep(0.2)

    # Clear results for phase 2
    with results_lock:
        results_received.clear()

    # --- Phase 2: Start AI client, test normal pipeline ---
    print()
    print("--- Phase 2: Starting mock AI client ---")
    ai_ready = threading.Event()
    ai_thread = threading.Thread(target=mock_ai_client, args=(ctx, ai_ready, stop_event))
    ai_thread.start()
    ai_ready.wait(timeout=3)

    # Wait for ZMQ monitor to detect connection
    print("--- Waiting 2s for ZMQ monitor connection detection ---")
    time.sleep(2.0)

    print()
    print("--- Publishing test image (with AI client) ---")
    publish_test_image(ctx)

    print()
    print("--- Waiting for pipeline result (max 5s) ---")
    deadline = time.time() + 5
    while time.time() < deadline:
        with results_lock:
            if results_received:
                break
        time.sleep(0.2)

    # --- Phase 3: Admin test ---
    print()
    print("--- Phase 3: Testing admin HTTPS endpoint ---")
    admin_ok = test_admin()

    stop_event.set()
    ai_thread.join(timeout=3)
    sub_thread.join(timeout=3)

    ctx.term()

    # --- Evaluate results ---
    print()
    print("=" * 60)
    print("TEST RESULTS")
    print("=" * 60)

    with results_lock:
        pipeline_result = results_received[0] if results_received else None

    all_pass = True

    def check(name, condition):
        nonlocal all_pass
        status = "PASS" if condition else "FAIL"
        if not condition:
            all_pass = False
        print(f"  [{status}] {name}")

    # Phase 1: No-client
    check("No-client: status result received", no_client_result is not None)
    if no_client_result:
        check("No-client: Status=0", no_client_result.get("Status") == "0")
        check("No-client: Reason present", bool(no_client_result.get("Reason")))

    # Phase 2: Normal pipeline
    check("Pipeline: Data Server -> Dealer -> AI Client -> Result Sub", pipeline_result is not None)
    if pipeline_result:
        check("Pipeline: Status=1", pipeline_result.get("Status") == "1")
        check("Result has TransactionID", bool(pipeline_result.get("TransactionID")))
        check("Result has detections", len(pipeline_result.get("Result", [])) > 0)
        check("Detection has LabelID", pipeline_result["Result"][0].get("LabelID") == "OCR")
        check("OCR text correct", pipeline_result["Result"][0].get("OCR") == "TEST123")

    # Phase 3: Admin
    check("Admin: HTTPS SetParameter command", admin_ok)

    print()
    if all_pass:
        print(">>> ALL TESTS PASSED <<<")
        sys.exit(0)
    else:
        print(">>> SOME TESTS FAILED <<<")
        sys.exit(1)


if __name__ == "__main__":
    main()
