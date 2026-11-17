# Wired protocol extensions — 2026-11-17

This checkpoint extends the real receiver, without changing the product's
dashboard. Phone, board and DHU acceptance remain NOT_RUN.

| Surface | Wire behavior and ownership |
| --- | --- |
| Version | `protocol_minor=0` retains 1.1. An explicit 1–7 ceiling accepts a successful lower 1.x response. Higher versions/nonzero status fail before TLS. Negotiated version is queryable; this is not full modern feature negotiation. |
| Navigation focus | Control 13 decodes type 1/2 and responds with 14, reporting the application's actual owner. Without a callback it reports native ownership. Invalid enums fail; no third indication is invented. |
| Voice session | Control 17 delivers START/STOP to an optional callback. No response is invented. Unknown optional protobuf fields are structurally validated and skipped. |
| Phone battery | Control 23 preserves optional level/time/critical presence. Explicit zero and absent level remain distinct. Levels above 100 fail. This is phone battery, not vehicle SOC. |
| Call availability | Explicit output API sends control 24 for negotiated minor >=4. Application supplies actual availability; this does not implement HFP or call audio routing. |
| Channel close | Legacy empty control 9 targets its envelope service channel. Stops that service, resets setup/binding/subscriptions, and permits reopen on the existing TLS session. Missing control flag and control-channel close fail. Modern transport-wrapper variants remain unverified. |
| Fragmented TX | FIRST/MIDDLE/LAST, plaintext total only on FIRST, separately encrypted fragments. Up to 65,518 microphone PCM bytes consume one ACK slot. Partial failure terminates the session without counting a completed upload. |
| TLS RX | Multiple complete TLS records can form one AAP fragment. Truncated/delayed records never spill into another channel. Records spanning AAP frames remain unsupported. |
| Input | Up to 32 configured unique keycodes, packed discovery/binding, 32-bit negotiated masks and signed rotary on scan code 65536. Legacy eight-key default and single-pointer API remain. Rotary cannot masquerade as a button. |
| Sensor scheduling | Optional paired start/update and stop callbacks preserve the exact nonnegative int64 interval. Teardown stops subscribed schedules. ACK write failure also stops a schedule already started. |

Reopen does not resurrect old input/subscriptions. Microphone session IDs
continue across reopen so old ACKs cannot release a new upload window. The
application still confirms hardware retirement before handing off resources.

## References and evidence

Definitions are compared with unmodified Open Android Auto at
`61eab61c5f9968154ff1a80faa8c0a427b208479`, specifically navigation 13/14,
VoiceSessionRequest, BatteryStatusNotification, CallAvailabilityStatus,
InputChannelConfig, InputBindingRequest and RelativeInputEvent. Fragment flags,
plaintext total and close body are cross-checked against AASDK at
`9bf6adf933665dee26532201719fac14a047ccf1`.

The independent Python protobuf oracle compares 14 fixtures. Real mbedTLS peer
tests exercise production framing and dispatch, including version downgrade,
actual navigation owner, optional battery presence, voice notifications, malformed
messages, multi-record/truncated TLS, rebind/resubscribe, service retirement,
microphone reopen/stale ACK, 16/32 KiB boundaries, maximum upload and a failure
midway through the second frame. They also cover the 32nd key bit, signed rotary,
duplicate/oversized key sets, int64 intervals and ACK-failure scheduler cleanup.

Windows CTest and Linux ASan/UBSan CTest pass 5/5. The actual RT-Thread/D213 SDK
archive compiles. Local evidence is `build/validation/protocol-followup-*`.
Integrated firmware is a separate product check. See `testing.md` for commands.

## Remaining work and wireless evidence

Still pending: wireless bootstrap/platform adapters, modern feature/service
updates, media metadata, navigation/phone status services, additional sensors,
multi-touch, microphone capture/AEC and complete product focus ownership.

Wireless research additionally inspected `nisargjhaveri/WirelessAndroidAutoDongle`
at `ef645221ec28e87361391b9bdcf27e31ff48cb6f` (GPL-3.0). Its four-byte
length/message header and start/credential IDs 1/2/3 agree with Prodigy. However,
the pinned Prodigy receiver labels IDs 6/7 differently from Open Android Auto's
newer WifiSetupMessage table. WifiInfoResponse and WifiSecurityResponse also
place key/BSSID differently. These cannot be combined into an invented universal
sequence. A parseable status is not evidence of successful TCP/AAP startup.
Wireless needs an explicit supported dialect, exact fixtures and platform-owned
SDP/pairing/AP/TCP lifetimes. No wireless implementation or default credentials
are claimed by this checkpoint.
