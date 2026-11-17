# Testing

Tests link the actual production C sources, nanopb runtime and mbedTLS. USB and media devices are replaced only at their public seams. The TLS/session tests run a real mbedTLS server with public test-only identities and real entropy, handshake messages, certificates and encrypted application records. They do not replace the production parser/codec/dispatcher with a mock protocol implementation.

| CTest group | Coverage |
|---|---|
| core | Incremental stream parsing, big-endian framing, sticky parser error, bounded random headers, interleaved independent assemblies, orphan fragment and huge-total rejection; AOA control sequence and malformed descriptors; partial writes/zero progress; FULLSCREEN/FUSION touch edges |
| protobuf | Independent ChannelOpen golden bytes with zigzag priority, required fields, optional presence versus explicit zero, unknown fields, varint overflow, malformed length |
| tls | Real TLS 1.2 handshake, certificate pin verification and rejection, encrypted roundtrip, altered record rejection, invalid credential configuration |
| receiver | Actual byte-at-a-time framing through TLS and receiver dispatch, noncanonical service IDs, discovery, all three PCM audio roles, H.264 sink and session ACK, touch/key, night sensor, focus, microphone window/ACK, shutdown, timeout, sink error without false ACK |
| rtthread | Actual RT-Thread port implementation against a minimal OS seam: 32-bit tick wrap and zeroed allocation/free |

Public headers are also compiled in separate C and C++ translation units. The Python protobuf oracle is supplementary: it compiles unmodified pinned upstream schemas and compares 27 message fixtures against the SDK subset. It does not cover the complete upstream schema or phone interoperability. The format check excludes unmodified third-party and generated code. Regeneration checks use pinned nanopb output byte-for-byte.

## Reproduction

```sh
cmake -S . -B build -DRTAA_MBEDTLS_SOURCE=/path/to/mbedtls-2.28.9 -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure
python scripts/check-format.py
python scripts/generate-proto.py --check
python scripts/verify-protocol.py --upstream /path/to/references/open-android-auto
```

For Linux ASan/UBSan, use a separate directory and `-DRTAA_SANITIZERS=ON`. Both SDK sources and the source-built TLS dependency are instrumented. For MinGW, use Ninja; MSVC CI uses Debug. A configured CMake build never fetches floating dependencies.

The supplied Windows D213 toolchain file uses the existing Luban-Lite c906fd flags: `rv64imafdc_xtheadc`, `lp64d`, `medany`. Example in PowerShell (quote each `-D` argument containing a path):

```powershell
cmake -S . -B build-d213 -G Ninja '-DCMAKE_TOOLCHAIN_FILE=C:/Project/Carplay/rt-androidauto/cmake/d213.cmake' '-DRTAA_D213_TOOLCHAIN=C:/Project/Carplay/luban-lite-carplay/toolchain' -DRTAA_TESTS=OFF -DRTAA_EMBEDDED_TLS_CONFIG=ON '-DRTAA_MBEDTLS_SOURCE=C:/Project/Carplay/references/mbedtls-aa' '-DRTAA_RTTHREAD_ROOT=C:/Project/Carplay/luban-lite-carplay/kernel/rt-thread' '-DRTAA_RTTHREAD_CONFIG=C:/Project/Carplay/luban-lite-carplay' -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-d213 -j8
```

This builds the SDK, actual RT-Thread port and client-only mbedTLS static archives. It does not link an integrated RT-Thread firmware executable and must not be reported as full target/firmware acceptance. The integration step must resolve the product's existing mbedTLS selection and symbol ownership before a complete firmware build.

## Remaining validation

The current random input check is a bounded deterministic smoke test, not a long-running coverage-guided fuzz campaign. Additional corpus and fuzz work is needed for encrypted fragment interleaving, allocator failure at every allocation, all message-order permutations, optional enum behavior and modern extension traffic. No hardware timing, cache coherency, USB cancellation latency, H.264 recovery, full mode switching, audio mixing, call routing or wireless handoff is established by these tests.

Use `HOST_TEST_PASS` only for the completed host checks. Record standalone archive compilation separately as `SDK_CROSS_COMPILE_PASS`. `TARGET_BUILD_PASS` and `FIRMWARE_BUILD_PASS` require the integrated target and complete image checks. Always retain `ANDROID_PHONE_NOT_RUN`, `D213_HARDWARE_NOT_RUN` and `GOOGLE_DHU_NOT_RUN` until those specific validations happen. See `delivery-status.md` for this local run's results and log locations.

The 2026-11-17 [wired extension checkpoint](wired-extensions.md) adds real TLS
regressions for control notifications, service close/reopen, fragmented TX,
multi-record RX, configured key masks/rotary and sensor scheduler retirement.
These extend the receiver test executable; five CTest groups remain.
