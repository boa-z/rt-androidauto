# Architecture

The core is a caller-driven C11 library. The byte transport is shared by wired USB bulk and a future wireless TCP port; there is currently no Bluetooth or Wi-Fi bootstrap implementation. AOA is a separate USB control state machine that completes before a receiver is started.

```text
Product worker and resource owner
    AOA control port -> re-enumeration -> bulk endpoint binding
    Byte transport -> AAP parser -> per-channel plaintext assembly
                                -> TLS 1.2 record decrypt/encrypt
                                -> nanopb decode + session dispatch
                                -> platform H.264/PCM/microphone/input sinks
```

The parser accepts arbitrarily divided stream reads and bounds the ciphertext payload at 18,432 bytes. FIRST without LAST adds a big-endian 32-bit plaintext total. Reassembly is separate for each registered service and control; fragments from different channels may interleave. Each fragment is decrypted before being appended to that channel's assembly. Attribute changes, orphan continuations, overlapping FIRST frames, total mismatches and oversized messages fail the session.

`receiver.c` owns protocol state: IDLE, VERSION, TLS, DISCOVERY, CONNECTED, CLOSING, CLOSED and FAILED. A successful TLS handshake sends AuthComplete; only then may discovery and channel traffic proceed. The service registry drives both discovery advertisements and incoming ID lookup. Channel open, AV setup, AV start and streaming are separate states. A media ACK uses the current AV session ID rather than a constant zero.

`tls_mbedtls.c` owns one client-side mbedTLS context and two bounded byte queues. The application supplies identity bytes, a random callback and either a CA/hostname policy or an explicit fingerprint policy. No default certificate, random seed, production key or blanket verification bypass is embedded. The receiver's current profile requires one complete TLS application record per AAP fragment; it rejects leftover or incomplete records instead of mixing their channel identity.

Audio is PCM little-endian. Media uses 48 kHz/stereo/16-bit; System and Speech use 16 kHz/mono/16-bit. Sinks receive the actual data and timestamps. Their success means that the frame has been consumed or queued. A failure is fatal and is not acknowledged as success. Microphone transmission observes the peer's bounded unacked window and validates ACK session/count values.

The core has no scheduling or device policy. It creates no thread and never calls a transport read/write with an infinite timeout. Terminal failure retires sinks and invokes cancel/close once. Reconnection requires a newly created object, transport and TLS session. Retry budget, USB bus arbitration and release-complete notification are product responsibilities that still need implementation.

All API calls and callbacks are serialized on the product worker. Calling an API recursively from its own callback is unsupported. Public snapshots must be copied or locked by the product if other threads need them. Firmware ports must keep configuration pointers, transport objects and callbacks alive until destruction.
