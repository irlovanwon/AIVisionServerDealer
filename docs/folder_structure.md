# AIVisionServerDealer — Folder Structure

Per [Coding Standard](../../Coding/folder_structure.md) — C++ project layout.

> Design documents (`.md` files) reside in this **local design folder only**.  
> The remote repository (EDGE01/EDGE02) contains software development files only (source code, build artifacts, configurations, scripts). See [Coding/rule.md §4](../../Coding/rule.md).

---

## Local Design Folder

```
AIVisionServerDealer/                   # THIS FOLDER (local only)
├── design.md               # System architecture & design
├── server.md               # Server connection & Git sync guide
├── API.md                  # API reference (API1a/1b/2)
├── config.md               # Configuration (ZMQ transport, API1/API2 settings)
├── folder_structure.md     # THIS FILE
├── parameter.md            # Admin parameter list
├── frame.md                # Frame specification for all API boundaries
├── API design/             # API design source materials (not deployed)
│   ├── AI interface.xlsx
│   └── Examples/
│       ├── Request-AI detection.json
│       ├── Response-AI detection.json
│       ├── Request-AI config.json
│       └── Response-AI config.json
└── AITest/                 # AI test resources (editable)
    ├── AITestServer.md     # AI test server documentation
    └── zmq_test_2.py       # ZMQ test script for AI server verification
```

> **RULE:** Do NOT change or update the AI client at `/home/user/Documents/ecid_2026_r1` on EDGE02 (`core_server.py` and related files). See [AITest/AITestServer.md](AITest/AITestServer.md).

---

## EDGE01 / EDGE02 Remote Folder

Path: `/home/user/ECIDS/AIVisionServerDealer`  
Repo: https://github.com/irlovanwon/AIVisionServerDealer.git

```
AIVisionServerDealer/
├── .gitignore               # Ignores: build/, certs/, *.pid, *.log
├── CMakeLists.txt           # CMake build configuration
├── README.md                # Project overview and quick start
├── config/
│   └── default.json         # Default configuration (JSON format)
├── include/ai_vision/
│   ├── common/
│   │   ├── Logger.h
│   │   ├── Response.h
│   │   └── Types.h
│   ├── api1/
│   │   ├── DetectionDealer.h       # API1a ZMQ DEALER/DEALER
│   │   └── AdminServer.h           # API1b HTTPS server
│   ├── api2/
│   │   ├── ImageSubscriber.h       # API2a ZMQ SUB
│   │   └── ResultPublisher.h       # API2b ZMQ PUB
│   ├── core/
│   │   ├── CoreModule.h            # Ingest pipeline, request/response queue
│   │   └── ImageBuffer.h
│   └── data/
│       └── ConfigManager.h
├── src/
│   ├── main.cpp                   # Entry point
│   ├── common/
│   │   ├── Logger.cpp
│   │   └── Response.cpp
│   ├── api1/
│   │   ├── DetectionDealer.cpp
│   │   └── AdminServer.cpp
│   ├── api2/
│   │   ├── ImageSubscriber.cpp
│   │   └── ResultPublisher.cpp
│   ├── core/
│   │   ├── CoreModule.cpp
│   │   └── ImageBuffer.cpp
│   └── data/
│       └── ConfigManager.cpp
├── tests/
│   ├── test_image_buffer.cpp      # Unit tests (gtest)
│   └── test_integration.py        # End-to-end integration test (Python/ZMQ)
├── scripts/
│   └── start.sh                   # start/stop/restart/status/build
├── certs/                         # TLS certificates (not tracked, auto-generated)
└── build/                         # Build artifacts (not tracked)
    ├── ai_vision_server_dealer    # Main executable
    ├── ai_vision_tests            # Test executable
    └── libai_vision_lib.a         # Static library
```

---

## Build Artifacts

| Artifact | Description |
|----------|-------------|
| `build/ai_vision_server_dealer` | Main application binary |
| `build/ai_vision_tests` | Google Test runner |
| `build/libai_vision_lib.a` | Static library (all source compiled) |

---

## Source Module Map

| Module | Headers | Sources |
|--------|---------|---------|
| **Common** | `common/{Types,Response,Logger}.h` | `common/{Response,Logger}.cpp` |
| **API1** | `api1/{DetectionDealer,AdminServer}.h` | `api1/{DetectionDealer,AdminServer}.cpp` |
| **API2** | `api2/{ImageSubscriber,ResultPublisher}.h` | `api2/{ImageSubscriber,ResultPublisher}.cpp` |
| **Core** | `core/{CoreModule,ImageBuffer}.h` | `core/{CoreModule,ImageBuffer}.cpp` |
| **Data** | `data/ConfigManager.h` | `data/ConfigManager.cpp` |

---

## Dependencies

| Library | CMake Find Method | Used By |
|---------|-------------------|---------|
| nlohmann_json (≥3.2.0) | `find_package` | Config, JSON frames |
| ZeroMQ (libzmq) | `pkg_check_modules` | API1a (DEALER), API2a (SUB), API2b (PUB) |
| OpenSSL | `pkg_check_modules` | TLS (API1b admin) |
| OpenCV | `pkg_check_modules` | Image handling |
| Google Test | `find_package` | Tests |

---

## Conformance

| Requirement | Status |
|-------------|--------|
| `src/` for source code | Done |
| `tests/` with `test_*.cpp` naming | Done |
| `config/` with JSON format | Done |
| `status/` (not tracked by Git) | N/A |
| `.gitignore` covers build/, certs/, status/ | Done |
| `CMakeLists.txt` build config | Done |
| `README.md` project overview | Done |
| File signature headers (VIATECH-GENERAL) | Done |
| Design docs local-only, not in repo | Done |

---

## Related Documents

| Document | Description |
|----------|-------------|
| [design.md](design.md) | System architecture & design |
| [server.md](server.md) | EDGE01/EDGE02 connection details and Git sync |
| [config.md](config.md) | Configuration file design |
| [AITest/AITestServer.md](AITest/AITestServer.md) | AI test server (ecid_2026_r1) setup and usage |
| [Coding/folder_structure.md](../../Coding/folder_structure.md) | Coding standard folder layout |
