# Wireless bootstrap development profile

`rtaa/wireless.h` implements a portable, heap-free Bluetooth RFCOMM bootstrap.
It is separate from AAP framing and takes an already authenticated/encrypted
stream. The platform starts its WPA2 AP and TCP listener **before** beginning.
An explicit user comparison must authenticate the Bluetooth peer before AP
credentials are released. The AAP receiver subsequently authenticates its TCP
peer using the existing provisioned TLS policy.

The implemented dialect follows the start/info/credentials sequence observed
in WirelessAndroidAutoDongle at `ef645221ec28e87361391b9bdcf27e31ff48cb6f`.
Reviewed schema reference: Open Android Auto
`61eab61c5f9968154ff1a80faa8c0a427b208479`, `oaa/wifi/`.
No reference project's platform/runtime code is embedded in this module.

RFCOMM service UUID: `4de17a00-52cb-11e6-bdf4-0800200c9a66`.
Frames contain a 2-byte big-endian **protobuf payload** length followed by a
2-byte big-endian message ID and the payload. The length excludes the header.

| ID | Direction/behavior |
|---|---|
| 1 | HU sends concrete IPv4 address field 1 and TCP port field 2 |
| 2 | Phone info request; well-formed unknown protobuf fields allowed |
| 3 | HU credentials: SSID=1, key=2, BSSID=3, security=4 (WPA2=8), AP type=5 |
| 6 | Connect status, signed int32 field 1, optional text field 2 |
| 7 | Start response, raw signed int32 status field 3; never redirects TCP |
| 8/9 | Ping request/response, optional int64 timestamp echoed |
| 10 | Explicit connection rejection terminates with a security error |
| 4/5/11, others | Explicit `RTAA_ENOTSUP`; modern setup/version profile pending |

This uses **WifiSecurityResponse**, not the unrelated WifiInfoResponse field
layout. The pinned Prodigy labels for IDs 6/7 conflict with the above dialect;
the code does not heuristically combine those protocols. Raw status 0 or 1
does not declare projection success. `WAIT_TCP` only means credentials were
written, and remains subject to the original total bootstrap deadline.

`begin` copies configuration and owns the stream only on success. Do not call
it again on a live object; close first. Poll performs one bounded partial I/O
operation. It accepts fragmented/coalesced input without losing frame boundaries,
limits each payload to 1024 bytes and validates protobuf syntax before dispatch.
Zero progress yields `AGAIN` until the configured deadline. Errors remain terminal
until the owner closes the object. Close calls cancel/close exactly once and
erases credentials and buffered output. Calls and callbacks are serialized.
Platform owners must release AP/listener/accepted TCP resources on every stop,
error, timeout and retry exhaustion, including a pending bootstrap with no AAP
receiver yet.

SSID is limited to 32 bytes; key is an 8–63-byte printable WPA2 passphrase or
64 hexadecimal digits. BSSID must be real, nonzero and unicast. There are no
fallback credentials, dummy addresses, test keys or trust-all TLS defaults.

Tests exercise byte-at-a-time and larger fragments, backpressure, duplicate info,
coalesced messages, raw statuses, ping, rejection, malformed/oversized messages,
unsupported version negotiation, timeout, close ownership and invalid settings.
Seven independent upstream protobuf fixtures cover the new schemas; the total
oracle now has 34 fixtures. Host tests do not establish wireless interoperability.

SDP, persistent bonds, pairing UI, real HFP, AP regulatory/channel policy, DHCP,
TCP and reconnect behavior are platform responsibilities. In particular, a phone
may require a genuine HFP hands-free connection before opening the bootstrap;
this module neither advertises nor simulates HFP. No phone/board/DHU acceptance
has run, and no minimum compatible Android version is claimed.
