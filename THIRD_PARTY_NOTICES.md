# Third-party notices

The combined SDK is GPL-3.0-only because its AAP framing adaptation incorporates AllPlay GPL-3.0 code. Open Android Auto GPL-3.0-or-later material is combined under GPL version 3. This does not relicense upstream components; individual notices continue to apply. The full GPL version 3 text is in `LICENSE`.

## AllPlay

- Source: https://github.com/UnmetDecade/AllPlay
- Reviewed commit: `9086b0d52c22e3cde3ff8e80872d9fba4590fb71`
- License: GPL-3.0; upstream author in the original framing source: blt.
- Material used: `core/projection/androidauto/aap.h` and `aap.cpp` as the basis for `src/framing.c`.
- Changes: C11 translation, fixed frame bounds, incremental parsing, explicit errors and per-channel bounded plaintext reassembly.
- Unmodified source headers, source and license are retained in `third_party/upstream/AllPlay/`. Its original third-party notices are also retained for provenance. The AllPlay CarPlay implementation, platform dependencies, credentials and backends are not incorporated.

## Open Android Auto

- Source: https://github.com/mrmees/open-android-auto
- Reviewed commit: `61eab61c5f9968154ff1a80faa8c0a427b208479`
- License: GPL-3.0-or-later.
- Copyright (C) 2024–2026 Open Android Auto contributors. Based on works by f1x.studio (Michal Szwaj), opencardev and aa-proxy, as noted by upstream.
- Material used: the bounded adaptation in `proto/aap.proto`, generated nanopb descriptors and reviewed field numbers/message IDs in the session dispatcher.
- Changes: combined message subset, shortened names, bounded arrays/strings, explicit subset of integer enum wire values. Original syntax and source comments remain authoritative for features outside that subset.
- Upstream license text is retained in `third_party/upstream/open-android-auto-LICENSE`.

## nanopb

- Source: https://github.com/nanopb/nanopb
- Version: 0.4.9.1, commit `cad3c18ef15a663e30e3e43e3a752b66378adec1`.
- Copyright (c) 2011 Petteri Aimonen and contributors; see the original file headers.
- License: zlib, retained unmodified in `third_party/nanopb/LICENSE.txt`.
- Runtime C sources/headers are copied unmodified. The nanopb Python generator is a development dependency, not part of the production archive.

## mbedTLS

- Source: https://github.com/Mbed-TLS/mbedtls
- Validated version: 2.28.9, commit `5e146adef63b326b04282252639bebc2730939c6`.
- Copyright The Mbed TLS Contributors.
- Upstream source offers Apache-2.0 OR GPL-2.0-or-later; the Apache-2.0 route is compatible with the combined GPLv3 SDK.
- External build dependency, not copied into this source directory. Preserve its notices and provide corresponding source with binary distributions as applicable. Test programs link upstream public test certificates and keys; these are not product credentials.

No source from the other reviewed reference repositories has been incorporated. Rust, Qt, Boost, libusb, BlueZ, OpenSSL, Android Java/JNI, LVGL, CarPlay, iAP2 and MFi are not SDK dependencies. Product firmware license/source obligations must be assessed using the final linked component manifest before release.
