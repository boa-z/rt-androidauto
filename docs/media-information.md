# Media information checkpoint

The optional `RTAA_MEDIA_INFO` role implements a text-only phone media information
receiver. It is separate from the PCM `RTAA_MEDIA` role and from CarLocalMedia.
No UI toolkit, platform driver or operating system is required.

## Reference and wire profile

Reviewed Open Android Auto commit:
`61eab61c5f9968154ff1a80faa8c0a427b208479`.
Sources are `oaa/control/ChannelDescriptorData.proto`,
`oaa/media/MediaChannelData.proto`, `MediaPlaybackStatusMessage.proto`,
`MediaPlaybackMetadataMessage.proto` and `docs/channels/media.md`.
The upstream has corrections/retractions; its confidence labels are research
claims, not interoperability acceptance for this SDK.

- Discovery advertises an empty media-info descriptor at field 9 on the
  configured, unique dynamic channel ID. Zero ID disables the service.
- Phone-to-HU `0x8001`: optional state (0..4), source application, elapsed seconds,
  shuffle, repeat and repeat-one, preserving each presence bit.
- Phone-to-HU `0x8003`: optional title, artist and album, including present empty
  strings. Album-art field 4 is skipped; fields 5..7 have uncertain semantics
  and are also skipped. Field 6 is **not** presented as duration.
- Strings are bounded to 255 bytes plus NUL. Longer messages fail decoding;
  no silent truncation is performed. Unknown fields must still have valid
  protobuf framing and fit the configured channel reassembly capacity.
- `0x8002` is a separate HU-to-phone cluster interaction, not a general playback
  command, and is unimplemented. Play/pause/next require the existing input
  channel with actual supported and bound media keycodes.

## Application contract

Enable `channels[RTAA_MEDIA_INFO]` and provide all three sink callbacks:
`media_status`, `media_metadata`, `media_info_reset`. Missing callbacks reject
configuration. Existing zero-initialized configurations keep this role disabled.

Notifications are individual optional-field updates, not merged snapshots.
Applications copy borrowed values before returning and choose their own merge
policy. Callback errors retire the session. Reset fires exactly once per opened
media-info channel on local/peer close, shutdown, disconnect or failure, even if
no notification was received; repeated teardown does not issue another reset.
Applications must clear any retained information there. Reopen begins a fresh
channel while the control/TLS session may remain connected.

No notification is ACKed or fabricated. Unopened, disabled and wrong-direction
traffic fails through the existing dispatcher. This profile does not implement
artwork delivery/rendering, media browsing, local-media projection or cluster
input. A phone may still expose additional messages requiring future review.

## Validation

Windows CTest and Linux ASan/UBSan exercise real TLS production dispatch with
noncanonical channel ID 155, exact discovery marker, the documented captured
status bytes, optional absent/empty/zero/false fields, unknown fields, 255-byte
text, malformed/overlong messages, callback rejection and channel close/reopen.
Independent unmodified upstream Python protobuf comparison covers descriptor,
status and text metadata, bringing the schema oracle to 17 fixtures. The
RT-Thread/D213 SDK archive is cross-compiled separately.

`ANDROID_PHONE_NOT_RUN`, `D213_HARDWARE_NOT_RUN`, `GOOGLE_DHU_NOT_RUN`.
