# rt-androidauto

Portable Android Auto Receiver SDK for RTOS and Embedded Systems.

[简体中文](README.zh-CN.md)

This is an experimental, independently buildable first SDK version. It contains production C11 source for USB AOA negotiation, bounded AAP framing/reassembly, a TLS 1.2 mbedTLS backend, nanopb service discovery and a receiver session with H.264, PCM audio, microphone flow control, input and basic sensors. It has no dependency on CarPlay, LVGL, ArtInChip, CherryUSB or a particular operating system. The optional RT-Thread port supplies an allocator and a monotonic clock.

**This is not the complete product integration.** Wireless Bluetooth/Wi-Fi bootstrap and combined CarPlay/Android Auto switching remain pending. A bounded portable reconnect controller and negotiated input lifecycles are implemented; the Luban-Lite project separately develops a wired backend and shared UI. Android phone, D213 hardware and Google DHU tests have not been run. Host protocol tests do not establish Android Auto interoperability or certification. See [compatibility and limitations](docs/protocol-compatibility.md) and the [local delivery report](docs/delivery-status.md).

## Build and test

Use an existing mbedTLS 2.28.9 source checkout. Configure never downloads dependencies. C11, CMake 3.20+ and a C/C++ host compiler are required; C++ is used only to compile a public-header compatibility check.

```sh
cmake -S . -B build -DRTAA_MBEDTLS_SOURCE=/path/to/mbedtls-2.28.9 -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

On Linux, add `-DRTAA_SANITIZERS=ON` to instrument the SDK, nanopb and mbedTLS with ASan/UBSan. On Windows with a Visual Studio generator, build and test with `--config Debug` and `-C Debug` respectively. MinGW/Ninja is also supported and was used locally.

The nanopb 0.4.9.1 C runtime and generated bounded message definitions are included. Python is needed only for regeneration and independent schema checks:

```sh
python -m pip install -r requirements-dev.txt
python scripts/generate-proto.py --check
python scripts/check-format.py
python scripts/verify-protocol.py --upstream /path/to/references/open-android-auto
```

The schema oracle requires upstream commit `61eab61c5f9968154ff1a80faa8c0a427b208479`. CI checks out this commit and mbedTLS commit `5e146adef63b326b04282252639bebc2730939c6` explicitly.

## Integration API

Start with `include/rtaa/core.h`, `include/rtaa/tls.h` and `include/rtaa/receiver.h`. Supply a bounded byte transport, credentials, secure randomness, an explicit peer trust policy and actual media sinks. Register each enabled service with its advertised channel ID and message capacity. The receiver resolves incoming IDs through this registry, including noncontiguous IDs above 127. A role's ordinal is never its wire channel ID.

The application owns one session worker and serializes API calls. Media callbacks consume or queue borrowed buffers before returning. A media sink error retires the session; the SDK does not discard audio and ACK it as successful. The core does not create threads, take over the USB host, decode H.264 or open audio hardware.

For bounded reconnection use `include/rtaa/connection.h`. The connector supplies one transport per attempt and synchronously retires unsuccessful pending opens. See [connection and input lifecycles](docs/connection-lifecycle.md).

The receiver also supports navigation focus, voice/battery notifications, explicit
call availability, legacy service close/reopen, fragmented microphone uploads,
configurable keys/rotary input, sensor scheduler callbacks and optional text media
information notifications, semantic navigation and phone status. See
[navigation and phone services](docs/navigation-phone.md),
[media information](docs/media-information.md) and
[wired extensions and remaining work](docs/wired-extensions.md). A version ceiling
can be configured; default 1.1 does not claim modern feature completeness.

## Documentation

- [Architecture](docs/architecture.md)
- [Porting, memory and trust policies](docs/porting.md)
- [Protocol profile and incomplete features](docs/protocol-compatibility.md)
- [Upstream research and reuse decisions](docs/upstream-research.md)
- [Testing and reproducible commands](docs/testing.md)
- [Luban-Lite integration preparation](docs/luban-lite-integration.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

The combined SDK is distributed under GPL-3.0-only, with the individual nanopb and protocol-source notices retained. No production credentials or private keys are included. Test-only keys come from mbedTLS's public test fixtures and are never linked into the SDK archive.
