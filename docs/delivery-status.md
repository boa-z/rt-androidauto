# Delivery checkpoints

## 2026-11-17 wired protocol extensions

The [wired extensions checkpoint](wired-extensions.md) adds navigation focus,
voice and battery notifications, call availability output, legacy channel
close/reopen, bounded fragmented TX, multiple complete TLS records per AAP frame,
configurable 32-key/rotary input and sensor scheduling callbacks. Windows and
Linux ASan/UBSan CTest pass 5/5, the upstream protobuf oracle passes 14 fixtures,
and the actual RT-Thread/D213 SDK archive compiles. Phone/board/DHU remain NOT_RUN.
Wireless and remaining service coverage are explicitly unfinished. Older tables
below retain their original checkpoint's historical scope.

## 2026-11-17 follow-up

The first SDK checkpoint was published as `86af84bbc69cfb97ead6ae552af8714642450dc4`. Its [Linux and Windows CI](https://github.com/boa-z/rt-androidauto/actions/runs/44038629241) both passed. The product now has a real submodule and an authorized `codex/androidauto-integration` branch. Product app and UI development is tracked in its own repository; the independent demo is `application/rt-thread/rtprojection`, with Android Auto as its first backend.

This SDK follow-up implements the [bounded connection owner and negotiated input lifecycle](connection-lifecycle.md). Windows and WSL ASan/UBSan CTest each passed 5/5; the real RT-Thread port and D213 archive compiled again. Evidence for the Linux follow-up is `build/validation/linux-sanitizer-followup.log`. No hardware or DHU test was run. Wireless discovery/bootstrap and combined product switching remain pending.

The remainder of this document preserves the initial checkpoint's scope and validation history; its NOT_RUN integration/publication rows describe that earlier checkpoint, not current product build results.

## Initial SDK checkpoint

This is the first independently buildable SDK review checkpoint, not completion of the entire Android Auto/product task. Source is in `C:\Project\Carplay\rt-androidauto`; research checkouts are under `C:\Project\Carplay\references`. The user authorized the initial SDK commit and push on 2026-11-16. This checkpoint is prepared for publication to `boa-z/rt-androidauto`; publication success and remote CI results must be verified separately. Product submodule registration remains pending the user-prepared development branch.

## Actual implementation

The SDK contains C11 production source for AOA negotiation and descriptor binding; incremental bounded AAP framing and per-channel plaintext assembly; a TLS 1.2 mbedTLS backend with credential/random/trust interfaces; nanopb runtime and a generated bounded protocol profile; service discovery and dynamic channel dispatch; basic H.264 and three PCM audio-role lifecycles; microphone window/ACK handling; input/focus/basic sensor messages; transport deadlines, shutdown and resource retirement; and an optional RT-Thread allocator/clock port. None of these modules depend on CarPlay or a device/UI SDK. Detailed unsupported behavior is listed in `protocol-compatibility.md`.

## Validation results

| Check | Result | Evidence / limits |
|---|---|---|
| Windows MinGW/Ninja compile | PASS | `build/validation/windows-build.log`; production source uses C11 and warnings as errors |
| Windows Host CTest | HOST_TEST_PASS, 5/5 | `build/validation/windows-ctest.log` |
| Linux WSL Ubuntu 24.04 ASan/UBSan | HOST_TEST_PASS, 5/5 | `build/validation/linux-sanitizer.log`; SDK, nanopb and source-built mbedTLS instrumented |
| Public C/C++ header compilation | PASS | CMake object targets; no C++ runtime in production library |
| Format / nanopb regeneration | PASS | `format.log`, `generated.log` under `build/validation` |
| Unmodified upstream Python protobuf oracle | PASS, six message families | `build/validation/protocol-oracle.log`; pinned Open Android Auto SHA |
| D213 SDK and actual RT-Thread port static compilation | SDK_CROSS_COMPILE_PASS | `build/validation/d213-build.log`; existing GCC 10.2.0 c906fd toolchain, actual product RT-Thread headers/config, standalone mbedTLS 2.28.9 client-only profile |
| Combined SDK/TLS relocatable link | PASS | `build-d213/rtaa-sdk.o` is ELF64 little-endian RISC-V; `d213-relocatable-link.log`, `d213-object.txt`, `d213-unresolved-symbols.txt`; no unresolved nanopb/mbedTLS symbols; OS/libc bindings still belong to a real product link |
| Existing CarPlay SDK full regression | PASS on full rerun | `build/validation/carplay-sdk-recheck.log`, exit 0. Initial run failed a lockdown TLS read assertion; isolated test rerun and subsequent full rerun passed. Initial failure remains recorded in `carplay-sdk.log`; its cause was not changed or established here. |
| Existing product Host regression | PASS | `build/validation/carplay-integration-isolated.log`, exit 0. Initial default build hit Windows `.d` dependency syntax in WSL; a fresh output directory passed. That generated directory was moved into this SDK's `build/validation/carplay-integration-build` afterward. |
| Integrated Android Auto target executable | NOT_RUN | Not represented by standalone archive compilation; real gitlink, adapters and product TLS ownership are not implemented |
| Complete integrated IMG/OTA/Bootloader build | FIRMWARE_BUILD_NOT_RUN | No integrated AA build exists; no image was generated or accepted for this change |
| Android phone | ANDROID_PHONE_NOT_RUN | No phone connected |
| D213 execution | D213_HARDWARE_NOT_RUN | No board connected or flashed |
| Google DHU | GOOGLE_DHU_NOT_RUN | Not launched |
| Remote SDK CI | NOT_RUN | Workflow source supplied; no push or remote workflow dispatch |

Host ABI wrapper storage is 154,664 bytes for the receiver and 75,984 bytes for TLS, excluding channel assemblies and mbedTLS internal allocations. These values are observations from the test binaries, not target peak RAM measurements. The product must budget buffers, RT stack, media queues and TLS internal heap separately.

## Product workspace preservation

At the start the product was on `carplay-oss` with five dirty launcher/fusion UI files. No SDK-development command modified those sources or CarPlay protocol source, `.config`, `.gitmodules` or firmware release policy. During this shared-workspace session the product HEAD advanced externally to `83c0c433`; the initially dirty UI changes are now represented by product commits and the final observed product status is clean. No product Git commits or history changes were made by this task. The extra product test output created by this task was moved out of the checkout after testing.

## Work still required

1. Publish the authorized first buildable SDK checkpoint and verify the remote commit and CI. The remote initially had no refs; a real published commit must precede submodule registration.
2. Prepare the intended product development branch manually, then register the real fixed gitlink after a remote commit exists. Apply SDK Kconfig once and keep package-scanner-only SCons compilation ownership. Verify missing-submodule diagnostics and disabled builds.
3. Implement full wireless SDP/RFCOMM/bootstrap, real Wi-Fi credential storage/exchange, AP/TCP transport and bounded reconnect. This version contains none of that wireless implementation.
4. Implement the product projection/resource owner and real CherryUSB, MPP, audio/capture, touch and network adapters. Resolve existing mbedTLS package/configuration ownership without duplicate symbols. Complete full pointer/key negotiation, richer focus, lifecycle/recovery and compatibility coverage before advertising product readiness.
5. Implement actual Connectivity status and FULLSCREEN/FUSION mode changes, combined regressions and product CI metadata; build and verify the complete firmware with the new protocol enabled and disabled.
6. Schedule separate phone/board acceptance later, including media quality, calls, assistant, mode margins, wireless pairing, reconnect and protocol switching. No such hardware acceptance is authorized or reported by this local checkpoint.

The authorized initial commit description is `feat: add experimental portable AOA/AAP receiver core and host tests`. It should explicitly state that wireless and Luban-Lite integration are pending. No release-readiness or phone-compatibility claim should accompany this first version.
