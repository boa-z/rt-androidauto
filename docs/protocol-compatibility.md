# Protocol compatibility and limitations

The current code implements an experimental AAP receiver profile (1.1 by default, configurable 1.1–1.7 negotiation). No Android version, phone model or modern Android Auto APK version has been accepted through device testing. The reviewed Open Android Auto snapshot documents APK 16.1–17.3, including uncertainties and retractions; reading those definitions is not interoperability evidence.

| Surface | Implemented in this first version | Boundary / remaining work |
|---|---|---|
| USB AOA | Requests 51/52/53, six identities, accessory IDs, endpoint descriptor validation, detach and deadline | Product has a dedicated-port CherryUSB adapter; real enumeration, DMA and disconnect validation remain pending |
| AAP framing | Big-endian headers, FIRST/MIDDLE/LAST, bounded stream parsing, per-channel plaintext reassembly | TX uses FIRST/MIDDLE/LAST with plaintext total size, bounded to 65,528 bytes including message ID; microphone PCM up to 65,518 bytes per ACK window slot |
| TLS | TLS 1.2 client, real encryption/decryption, certificate/key load, CA or explicit fingerprint verification | Multiple complete application records per fragment are supported; a TLS record spanning AAP frames fails explicitly; real Android trust policy not provisioned |
| Session | Version, TLS, AuthComplete, discovery, dynamic registry, channel open, heartbeat, shutdown, terminal failure and optional bounded reconnect controller | Navigation focus request/response, voice notifications, battery notifications, explicit call-availability output, and legacy envelope-targeted channel close/reopen are implemented. Full modern feature negotiation and service updates remain absent; platform release acknowledgements remain host responsibilities |
| Video | H.264 capability, setup codec validation, start/session/config, stop, focus callback, timestamp, sink, ACK and byte statistics | No H.264 parsing/recovery or decoder implementation, product has an MPP/DE adapter; no measured buffering or phone validation |
| Audio | Media/System/Speech PCM capability, start/stop, timestamped sink, audio focus callback and ACK | SDK remains PCM-only; product supplies basic conversion, mixer, volume and sound0 playback. Complete call routing, quality, latency and phone focus semantics unverified |
| Microphone | 16 kHz mono PCM, open/close callback, EC/ANC forwarding, timestamped transmission, bounded ACK window | Capture device and backpressure scheduling not integrated; speech assistant/calls unverified |
| Input | Configurable set of up to 32 confirmed keys, paired key down/up, signed rotary delta, single-pointer lifecycle, viewport/margin transform, release/cancel at last valid position | Multi-touch and key repeat are unsupported; local gesture capture and panel routing remain application responsibilities |
| Sensors | Advertised Night and DrivingStatus, subscription validation/events and start/update/stop scheduler callbacks preserving signed 64-bit intervals | No fake vehicle values are emitted; product supplies real driving restrictions; refresh scheduling remains product work |
| Media information | Optional dynamic descriptor field 9, incoming status 0x8001 and text metadata 0x8003, typed callbacks and reset on channel retirement | Strings limited to 255 bytes; album art and metadata fields 5–7 are skipped. Cluster interaction 0x8002, media browsing and CarLocalMedia remain unsupported |
| Navigation status | Optional semantic descriptor field 8, explicit start/stop, state 0x8003, legacy text turn 0x8004, semantic steps/lanes/destinations 0x8006 and position/ETA 0x8007 | Bounded text/arrays; no icon bytes, legacy 0x8005 or energy forecast 0x8008; no NEXT_TURN_IMAGE capability or full modern CarInfo negotiation |
| Phone status | Optional empty descriptor field 10, call/signal notifications 0x8001, explicit d-pad/cluster input 0x8002 and reset callbacks | Four calls, 127-byte strings; contact photos skipped; Bluetooth HFP, dial/answer/hangup, DTMF, contacts sync and audio routing are separate and unimplemented |
| Wireless | Portable bounded RFCOMM bootstrap, start/credentials, raw status, ping, rejection and deadline; AAP byte transport can be supplied by TCP | Explicit Dongle profile only; modern version/setup messages unsupported. SDP/pairing/bonds, genuine HFP, AP/DHCP/TCP are platform responsibilities. See [wireless.md](wireless.md); phone interoperability unverified |
| Product | SDK Kconfig/SConscript and optional RT-Thread allocator/clock; real fixed gitlink, independent rtprojection app, USB/media adapters, resource arbiter, shared original dashboard and complete image build | Phone, board, DHU and same-image CarPlay/AA switching remain unverified or incomplete |

## Schema profile

`proto/aap.proto` is a bounded subset adapted from Open Android Auto at `61eab61c5f9968154ff1a80faa8c0a427b208479`. nanopb 0.4.9.1 supplies the real C wire codec. Required fields, optional presence bits, signed zigzag ChannelOpen priority, varint bounds, unknown field skipping and bounded repeated arrays are tested. The independent Python protobuf oracle compares 27 message fixtures with the unmodified upstream schema.

This is not a verbatim or complete upstream schema distribution. Message names are shortened, selected enum fields are represented as integer wire values with dispatch validation, unused fields are skipped, the repeated touchscreen capability is emitted as a single message, and arrays/strings have explicit static bounds. Unknown proto2 enum preservation and the full upstream enum semantics are not claimed. The input key list is limited to 32 entries (the legacy eight-key default is retained); discovery advertises at most ten services. A decoder rejects messages exceeding those bounds.

Important corrections carried into this profile: AV Setup field 1 is the media codec, not a configuration index; ChannelOpen priority is signed zigzag; AVStart carries required session and config; ACK uses that session; Speech=1, System=2, Media=3; VideoFocusRequest uses fields 2 and 3; InputEvent touch is field 3. Sensor request uses required sensor type and signed int64 interval. Video margins represent total width/height, not each edge.

Unsupported messages fail explicitly in the current dispatcher. This conservative behavior may need adjustment for optional extensions after trace-based review. Session tests use an independent real mbedTLS server with test message injection, rather than a phone. They exercise production parsing, TLS and dispatch; they do not independently prove every injected message's real-phone direction or timing. `ANDROID_PHONE_NOT_RUN`, `D213_HARDWARE_NOT_RUN` and `GOOGLE_DHU_NOT_RUN` remain mandatory labels.

## Extension checkpoint

See [wired extension implementation and evidence](wired-extensions.md). A configured
version is a negotiation ceiling, not a statement of complete support for that
version. Default version and capabilities remain conservative. Product UI is
unchanged by these SDK additions. Wireless reference differences and remaining
work are recorded explicitly; no RFCOMM/Wi-Fi implementation is claimed here.
