# Navigation and phone status services

These optional services extend the portable receiver; both remain disabled in
zero-initialized configurations. They do not depend on the UI or Bluetooth HFP.
All callback arguments and text pointers are borrowed until the callback returns.
Applications must deep-copy text they retain. Optional fields are preserved,
including absent nested messages versus present empty messages. Events are
individual updates; the application owns any merge policy.

## Reviewed reference

Open Android Auto `61eab61c5f9968154ff1a80faa8c0a427b208479`:
`oaa/control/ChannelDescriptorData.proto`, `oaa/navigation/NavigationChannelData.proto`,
`NavigationStateMessage.proto`, `NavigationNotificationMessage.proto`,
`NavigationTurnEventMessage.proto`, `InstrumentClusterMessages.proto`, and
`oaa/phone/PhoneStatusMessage.proto`, `PhoneCallStateEnum.proto`,
`PhoneStatusInputMessage.proto`. Message directions are cross-checked against
`docs/channels/nav.md` and `docs/channels/phone.md` at that snapshot. Their
confidence/retraction notes remain relevant; schema equality is not phone testing.

## Navigation

Configure `channels[RTAA_NAVIGATION]` with a unique ID, all five navigation sink
callbacks, and an optional nonnegative `navigation_interval_ms` (0 uses 1000).
Descriptor field 8 advertises required minimum interval and semantic
TURN_BY_TURN=1. Image options and NEXT_TURN_IMAGE are not advertised.

After the channel opens, explicitly call `rtaa_receiver_navigation(receiver,true)`
to send the empty cluster-start message 0x8001. False sends stop 0x8002. Repeated
state is idempotent. Before open the function returns ESTATE without output.
Channel retirement resets subscription ownership; reopen requires a new start.
There is no automatic version/PDK inference or fabricated display availability.
Applications should start only when their consumer is ready.

Phone-to-HU callbacks:

- 0x8003: optional state: 0 unavailable, 1 active, 2 inactive, 3 rerouting.
- 0x8004: deprecated flat turn text and numeric fields; legacy icon bytes skipped.
- 0x8006: semantic steps (maneuver/roundabout values, instruction, lanes with
  recommended flags and road names) and destinations with charging details.
- 0x8007: optional current-step distance/time, repeated destination distance,
  ETA text and signed int64 remaining seconds, plus optional current road.
  Stationary messages containing only distance display text are accepted.

No physical units or ranges are inferred for unclear legacy numeric fields.
Signed values remain signed. Known enum values are validated; maneuver values
30/31 are holes in the reference enum and are rejected. Time values are not
narrowed to 32 bits. The similarly named NavigationDistance is phone-internal
and is **not** decoded as 0x8007. Control-channel navigation focus remains a
separate interface and does not subscribe to this service.

Limits: four steps, six lanes per step, three directions per lane, three road
names per step, two destinations, and 127-byte text fields. Counts exceeding
these limits reject the message before callbacks; none are silently truncated.
Unknown protobuf fields are skipped with framing validation. Legacy 0x8005,
energy forecast 0x8008, images and full modern CarInfo feature negotiation remain
unsupported and fail explicitly if dispatched.

## Phone status

Configure `channels[RTAA_PHONE]` and both `phone_status` and `phone_reset`
callbacks. Descriptor field 10 is an empty phone-status marker. 0x8001 from the
phone contains up to four calls and optional raw signal strength. Calls require
state (0 unknown, 1 in call, 2 on hold, 3 inactive, 4 incoming, 5 conferenced,
6 muted) and elapsed seconds. Optional number, display name and contact ID are
preserved, bounded to 127 bytes. Photo bytes are skipped. Empty calls means idle.
No arbitrary signal-strength scale or maximum is imposed.

`rtaa_receiver_phone_input` sends HU-to-phone 0x8002 with a required nested
action (0 unknown, 1 up, 2 down, 3 left, 4 right, 5 enter, 6 back, 7 call) and
optional caller ID/display name. NULL is absent; an empty string is present.
CALL=7 is the reference's cluster action, not a verified dial/answer/hangup API.
It is never emitted automatically. HFP/audio call routing, DTMF and contact
synchronization are separate capabilities and are not implemented here.

## Lifecycle and validation

Every enabled service requires its real callbacks, including reset. Reset runs
once for each opened channel on local/peer close, shutdown, failure or destroy;
repeated retirement does not issue another reset. Callback rejection and TX
errors retire the session. Open-channel notification delivery tolerates late
notifications after stop; stop is not a claim of immediate phone quiescence.
Incoming HU-to-phone message IDs fail direction validation through dispatch.

The production receiver tests use a real TLS peer, noncanonical IDs 163/229,
all ten simultaneous discovery roles, captured stationary-distance bytes,
optional empty/absent/zero/false values, nested data, int64 time, required fields,
enum/bounds failures, direction, close/reopen, reset and callback/TX failure.
The independent unmodified upstream Python oracle now compares 27 fixtures.
Windows/Linux host checks and D213 archive compilation are software evidence.

`ANDROID_PHONE_NOT_RUN`, `D213_HARDWARE_NOT_RUN`, `GOOGLE_DHU_NOT_RUN`.
