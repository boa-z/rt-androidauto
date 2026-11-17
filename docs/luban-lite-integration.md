# Luban-Lite integration preparation

This document records inspected integration boundaries and the next implementation work. It is not an assertion that the SDK is integrated. The product checkout was `C:\Project\Carplay\luban-lite-carplay` on `carplay-oss`; five existing launcher/fusion UI files were dirty. They were preserved. No product source or workflow was changed, no branch/commit/push was made, and no fake submodule was created.

## Required sequencing

The remote `boa-z/rt-androidauto` initially returned no refs. The user authorized initial SDK publication on 2026-11-16. Verify its published commit before registering the submodule. The product user must prepare the intended development branch. Only after a real remote commit exists may `packages/third-party/rt-androidauto` be registered as a real gitlink with the specified GitHub URL. Do not copy the SDK into that path, substitute a symlink or add a floating CI clone.

## Build ownership

The product's `packages/third-party/SConscript` scans every child with a SConscript. This SDK's SConscript is the sole compilation owner and returns no objects when `LPKG_USING_RT_ANDROIDAUTO` is disabled. Do not also invoke it from the application SConscript. The application should compile adapters and add public include paths only. Source its Kconfig exactly once using the established CarPlay pattern, and provide an explicit initialization error if Android Auto is enabled but the gitlink is absent.

`RTAA_MBEDTLS_INCLUDE` is an SDK portability seam, not yet a working product default. The standalone check uses mbedTLS 2.28.9 and a client-only configuration. The product `.config` currently advertises a stock 2.7 package, while the CarPlay submodule also has 2.28.1 headers for its crypto build. Determine which symbols/configuration the final firmware actually owns; do not link a second conflicting SSL/crypto instance or assume header compatibility proves binary compatibility. Resolve this before reporting target integration success.

## Resource and session ownership

`application/rt-thread/rtcarplay/app/connection_manager.c` already owns CarPlay stage transitions, USB/wireless choice, retry state, pairing and display reconfiguration. Its public status contains real receiver/media/input counters. `platform/rtcp_usb_lubanlite.c` owns CherryUSB adaptation and Apple configuration selection. The current application SConscript includes existing video/audio/capture/touch backends selectively. Incremental integration must work through these owners; a second independent USB host manager would race them.

Implement an application projection owner which distinguishes Apple descriptors from Android accessory candidates before starting either protocol. Do not issue AOA requests indiscriminately to Apple or arbitrary USB devices. Keep one USB host controller/resource owner. On protocol change, request the old session's stop, wait for I/O cancellation and decoder/audio/capture release to complete, then grant resources to the other protocol. Receiver destruction alone is not proof that asynchronous DMA/device work is complete.

Reuse the existing MPP/DE/audio/capture/GT911 implementations behind neutral adapter functions, keeping protocol-specific PCM framing and coordinate metadata in the application layer. Do not add a dependency from this SDK to `rt-carplay` types. Sink callbacks must enqueue actual media to the owning device; no audio discard placeholder should be exposed as available capability.

## Layout and UI

Retain the `rtcarplay` application path and Mobility Cockpit V4. FULLSCREEN is 1024×600; FUSION uses the left 768×600 projection viewport plus the right 256×600 LVGL strip. At 1280×720 advertise total margins 256×120 or 512×120 respectively. Decode the full H.264 frame and then crop symmetric margins. Map only projection touches with the negotiated transform; LVGL strip touches belong to the local UI. Mode changes close/recreate the AA session and report unsupported configurations. Actual phone margin acceptance is unverified.

Only after the application is wired should Connectivity replace Planned with real Android Auto state. Existing language/theme/brightness/volume/OTA behavior must remain intact. Coordinate changes to the five preexisting dirty UI files with their owner; do not overwrite them during integration.

## CI and release

The inspected `.github/workflows/firmware.yml` already uses recursive submodules and publishes a rolling preview from `carplay-oss`. `.ci/build-firmware.sh` already records recursive submodule SHAs in `source-manifest.txt`. Preserve this release policy. After real gitlink integration, add SDK host/sanitizer and combined application regressions, check one SDK compilation owner, and ensure the manifest includes the new fixed gitlink SHA. Preserve IMG, OTA, bootloader, version and checksum assets. No push or CI release is authorized by this local development task.

## Diagnostics and later device acceptance

Expose the projection owner, protocol, AOA stage, selected endpoints, AAP state, trust failure, channel-role mapping, AV session, ACK/outstanding counts, byte counters and last failure without printing credentials. Distinguish unsupported codec/geometry, parser rejection, transport timeout, media enqueue failure and ownership conflict. Bound reconnect attempts at the product owner and reset the budget only on an explicit policy event.

Later acceptance must cover cold boot, physical re-enumeration on the same USB route, unplug during TLS/media, repeated reconnect, switching both protocols, drain completion, wireless pairing/AP/TCP handoff, touch boundaries/mode switching, video recovery, all audio roles, microphone/assistant/calls, saved credentials and OTA. This task has run none of those phone/board tests.
