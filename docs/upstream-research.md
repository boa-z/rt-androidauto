# Upstream research and reuse

These ten repositories were cloned under `C:\Project\Carplay\references` for local inspection. The table records the actual inspected commits, not a floating branch. Last-commit dates describe those snapshots and do not establish maintenance guarantees. No reference project was built or exercised with a phone during this work. Functionality claims in their READMEs remain author claims.

| Project / inspected SHA | Snapshot date | License evidence | Findings and RTOS cost |
|---|---|---|---|
| [opencardev/aasdk](https://github.com/opencardev/aasdk/tree/9bf6adf933665dee26532201719fac14a047ccf1) | 2026-06-11 | GPLv3 source headers; no root LICENSE in this checkout | Broad protocol/service implementation and upstream schema lineage. Current build includes C++, Boost/Asio, OpenSSL, libusb, protobuf/Abseil. A complete direct RTOS port would carry substantial event-loop/runtime work. Used for comparison, no copied runtime code. |
| [mrmees/open-android-auto](https://github.com/mrmees/open-android-auto/tree/61eab61c5f9968154ff1a80faa8c0a427b208479) | 2026-07-26 | LICENSE and per-file GPL-3.0-or-later | Protocol reference, not a receiver SDK. Distinguishes APK observations, captured wire evidence and unverified fields. Supplies the adapted bounded schema and independent oracle. Retractions matter: older names and field interpretations are not automatically valid. |
| [koenieee/esp32-android-auto-nav](https://github.com/koenieee/esp32-android-auto-nav/tree/12b476f1fc730909620bf54a616e623080eb5760) | 2026-03-17 | Cargo LGPL-3.0-or-later; no standalone root license file in checkout | Rust + ESP-IDF + mbedTLS, synchronous embedded receive loop and constrained memory. README reports 3–5 fps software H.264 and audio stubs which discard payloads. Describes phone joining AP/TCP; this is not proof of complete production Bluetooth wireless bootstrap. Reviewed as an embedded feasibility reference. |
| [UnmetDecade/AllPlay](https://github.com/UnmetDecade/AllPlay/tree/9086b0d52c22e3cde3ff8e80872d9fba4590fb71) | 2026-11-15 | GPL-3.0 LICENSE and THIRD_PARTY_NOTICES | Small AOA/AAP backend; its README explicitly says wired Android Auto is not phone-tested and has no UI entry. C++17, std::thread/mutex/vector and OpenSSL are not a direct C11/RTOS fit. Framing source was translated and bounded. The backend's fixed IDs, single global assembly, constant ACK session and limited capabilities were not copied into the receiver. |
| [uglyoldbob/android-auto](https://github.com/uglyoldbob/android-auto/tree/06c68d5ea8ac4f0ef0ab3b97dc60b9665f7074cb) | 2026-09-28 | Cargo LGPL-3.0-or-later; no standalone root license file in checkout | Reusable Rust library and desktop receiver, optional USB/Bluetooth; latest snapshot includes an interleaved-fragment decrypt fix. Tokio/futures, rustls, AWS-LC and a patched webpki are sizeable RTOS/runtime dependencies. Useful for transport/reassembly comparison; not copied. |
| [opencardev/openauto](https://github.com/opencardev/openauto/tree/4cc739b813622739b09352655581072fc4d39280) | 2026-02-08 | GPLv3 LICENSE | Qt application layered over aasdk with established media-session structure. Platform media/UI dependencies and its application lifecycle make direct SDK extraction larger than the portable subset. No copied code. |
| [mrmees/openauto-prodigy](https://github.com/mrmees/openauto-prodigy/tree/3afc5dd8bbacb5ce68e109189b18b14b251fabef) | 2026-08-06 | GPLv3 LICENSE | Modern wireless receiver application, Qt6/QML, Linux BlueZ/Wi-Fi and plugins. Strong reference candidate for the future real wireless bootstrap and reconnect work. No copied code and no local phone verification. |
| [shihabal3amri/DiAuto](https://github.com/shihabal3amri/DiAuto/tree/f3046721814bff289a78e58f597b45d895efbb95) | 2026-10-22 | AGPLv3 LICENSE | Android/BYD application using Android UI, connectivity and JNI/native stack; author claims wired/wireless receiver operation. Product-specific dependencies make it unsuitable as a direct RTOS SDK. AGPL is allowed by the task; dependency/porting cost, not license preference, excludes it from this first adaptation. |
| [andreknieriem/open-headunit](https://github.com/andreknieriem/open-headunit/tree/976fa05850bfe08eb9d15201463b1ba187b10219) | 2026-11-16 | AGPLv3 LICENSE | Android receiver with native protocol lineage and legacy Android compatibility. README warns Android Auto 17.4+ can block legacy wireless triggers. Useful compatibility warning; Android APIs are not a direct RTOS port. No copied code. |
| [PiggyBackBayonet/MiAuto](https://github.com/PiggyBackBayonet/MiAuto/tree/8ee36108ab74a58cbb0ef5c305e813a5f2fd8f41) | 2026-11-03 | AGPLv3 LICENSE | Its current README says the phone-side protocol was replaced by ICCOA CarLink for CarWith/Car+/Jovi InCar. It is not a drop-in Android Auto protocol implementation at this SHA. No copied code. |

## Reuse decision

The implementation reuses nanopb's existing tested C codec directly and adapts the community protocol definitions instead of inventing a new serialization implementation. AAP framing is a C11 adaptation of the small AllPlay implementation with the original sources and notices retained. mbedTLS supplies the actual cryptography and TLS state machine. The product SDK's combined license is GPL-3.0-only; there was no attempt to select a permissive license by rewriting licensed material.

The full AASDK, OpenAuto and Rust applications were not copied wholesale because their scheduler, language runtime and media/platform dependencies do not fit the requested C11 portable core directly. Their richer state machines remain important follow-up references. This first receiver dispatcher is deliberately smaller and does not claim their feature completeness, reliability history or modern compatibility.

Before implementing wireless, compare OpenAuto Prodigy's actual RFCOMM/SDP/Wi-Fi state machine and Open Android Auto's field-level evidence. In particular, do not advertise developer Head Unit Server TCP access as production wireless startup, and do not equate an AP/TCP demo with verified Bluetooth pairing and automatic launch.

## Wireless follow-up reference

On 2026-11-17, additionally inspected
[WirelessAndroidAutoDongle](https://github.com/nisargjhaveri/WirelessAndroidAutoDongle/tree/ef645221ec28e87361391b9bdcf27e31ff48cb6f)
at `ef645221ec28e87361391b9bdcf27e31ff48cb6f` (root GPL-3.0 LICENSE).
It uses BlueZ SDP/profile registration, RFCOMM start/credentials and TCP-to-USB
forwarding; it is not a native AAP receiver. No runtime code was copied or run.
See `wired-extensions.md` for differences between its credential exchange,
Prodigy's status IDs and the newer protocol-reference table.
