# Porting

## Required platform seams

Implement `rtaa_transport` for bounded reads, partial writes, cancellation, close and a monotonic clock. A return of zero means no progress; the write-all helper treats it as timeout to avoid an unbounded busy loop. Negative values are SDK error codes. The port must interrupt pending I/O when cancelled and must report physical disconnection. `rtaa_receiver_poll` reads at most 2,048 bytes per call; event-driven ports may call `rtaa_receiver_feed` directly and call poll for deadlines and heartbeat.

AOA uses `rtaa_usb_control` rather than a USB-stack header. Map requests 51/52/53 directly, return the actual data length, and pass all six NUL-terminated identity strings. Preserve physical port identity across accessory re-enumeration; VID/PID is insufficient. After the accessory returns, pass the complete USB configuration descriptor to `rtaa_aoa_bind`. Bulk endpoints must belong to a single alternate-zero FF/FF/00 accessory interface. Audio-only accessory IDs are rejected. USB detach must call `rtaa_aoa_detach` and retire any receiver. `rtaa_aoa` must be zero-initialized before first use.

Create one receiver with a service registry, desired geometry, media callbacks, transport and TLS configuration. Channel IDs are chosen by the product and are serialized in discovery; keep them unique and nonzero. Set an ID to zero to disable that service. Do not enable a media service without real start/data/stop callbacks. The product must implement H.264 decoder lifecycle, PCM mixing/volume/ducking, capture and hardware ownership.

## TLS and credentials

mbedTLS 2.28.9 is the validated standalone dependency. The library is supplied by the platform or by `RTAA_MBEDTLS_SOURCE`. `config/rtaa_mbedtls.h` is a small client-only profile compiled during the D213 SDK check. It has no socket, filesystem or entropy implementation. Provision a cryptographically secure random callback; do not use `rand`, a tick counter or a fixed seed.

`rtaa_tls_create` requires a matching private key and certificate. PEM lengths include the trailing NUL. Use an application credential provider to load these bytes and keep secrets out of logs and Git. The SDK parses and retains the identity in mbedTLS objects; application configuration pointers still need a well-defined lifetime. Destroy the receiver on disconnect; TLS state and byte queues are zeroed during destruction.

The default trust route requires CA bytes plus a hostname and uses mbedTLS verification. Android Auto deployments may require a provisioned fingerprint policy because a normal Web PKI chain is not available. The fingerprint callback receives a SHA256 hash of the actual leaf certificate. A zero result explicitly accepts that fingerprint and supersedes ordinary chain/time verification; compare to a provisioned pin and implement pin retirement in the product. No blind-accept policy is provided. Real-phone trust provisioning and compatibility remain unresolved.

mbedTLS internal allocations use its configured allocator. The wrapper's `rtaa_allocator` covers its wrapper object; it does not redirect mbedTLS's internal allocator. Enforce a separate heap budget through the mbedTLS platform allocator where the product needs a strict aggregate ceiling.

## Memory and scheduling

Use `rtaa_receiver_storage_size` and `rtaa_tls_storage_size` to measure wrapper storage on the target ABI. Receiver storage includes a 64 KiB control message assembly, a bounded frame parser and plaintext/ciphertext/send buffers. Add each enabled channel's configured capacity; a video capacity of 512 KiB is a starting point, not a measured phone requirement. Nonvideo messages normally fit in 16–32 KiB. `memory_limit` rejects a configuration whose wrapper-plus-assembly requirement exceeds it and checks arithmetic overflow.

Synchronous send helpers use approximately 4 KiB stack for protobuf encoding; microphone transmission uses an eight-byte timestamp prefix and the receiver's existing fragment buffer instead of a message-sized stack copy. Provision at least 32 KiB worker stack and measure high-water usage on hardware. Media data is borrowed during callbacks; asynchronous consumers must copy or enqueue to buffers they own. The portable core makes no assumption about DMA alignment or cache maintenance.

The optional RT-Thread port calls RT-Thread only from `port/rtthread`. Its clock extends unsigned 32-bit ticks across wrap and must be sampled at least once per wrap period. The core itself uses no RT-Thread API. No threads are created by this minimal port; the product owns the worker, mailbox and all calls to the receiver.

## Display geometry

For 1280×720 full frames, set total margins to 256×120 for a 1024×600 FULLSCREEN viewport, or 512×120 for a 768×600 FUSION viewport. The decoder must first receive the entire frame; the product crops symmetric margins and scales into the viewport. `rtaa_touch_map` maps viewport pixels into the full-frame coordinate space and rejects pixels outside it, including the right LVGL strip. Actual phone margin interpretation remains unverified.

The geometry is immutable for one receiver. To switch modes, complete shutdown and hardware release, destroy the old receiver, and create a new receiver with the requested geometry. A product must preserve pointer release/capture semantics when a touch crosses the viewport boundary. Unsupported phone geometry must be reported explicitly; it must not silently reuse a mismatched touch transform.
