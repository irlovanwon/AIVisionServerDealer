# Test1 — AI Server Batch Detection Results

**Date:** 2026-06-23  
**AI Server:** core_server.py on EDGE02 (Jetson Orin, cuda:0)  
**YOLO Weights:** ecid_26_20260612.pt  
**Test Script:** batch_test.py (ZMQ REQ → tcp://127.0.0.1:5555, Mode="File")

---

## Summary

| Metric | Value |
|--------|-------|
| Total images | 354 |
| Success | 354 (100%) |
| Failures | 0 |
| Total time | 233.7s |
| Throughput | 1.5 img/s |

---

## Resource Usage (EDGE02 during test)

| Resource | Min | Max | Avg |
|----------|-----|-----|-----|
| CPU | 0.0% | 27.0% | 5.4% |
| RAM | 4,133M | 6,046M | 5,918M (of 15,655M) |
| GPU | NVIDIA Orin (Jetson iGPU) — nvidia-smi utilization N/A on Jetson platform |

---

## Task1 Results (122 images: 61 left + 61 right)

**Naming:** `{a}_{b}.bmp` — a=1 (left camera), a=2 (right camera), b=paired index

| Camera | Images | Detections (total) | Avg detections/image |
|--------|--------|--------------------|-----------------------|
| Left (1_*) | 61 | 601 | ~9.8 |
| Right (2_*) | 61 | 1,057 | ~17.3 |

All 122 Task1 images returned detections (3–28 per image).

---

## Task2 Results (232 images: 116 left + 116 right)

**Naming:** `{a}_{b}.bmp` — a=0 (left camera), a=1 (right camera), b=paired index

| Camera | Images | Images with detections | Images with 0 detections |
|--------|--------|------------------------|--------------------------|
| Left (0_*) | 116 | 2 | 114 |
| Right (1_*) | 116 | 14 | 102 |

Task2 images are mostly blank/no-target — majority returned 0 detections. A few images returned 1–5 detections.

---

## Files

| File | Description |
|------|-------------|
| `results/` | 354 JSON result files (`{Task}_{a}_{b}.json`) |
| `batch_test.py` | Test script used |
| `resource_monitor.log` | CPU/RAM samples (2s interval) during test |
