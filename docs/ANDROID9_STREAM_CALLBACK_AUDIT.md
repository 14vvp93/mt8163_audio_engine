# Android 9 stream callback contract audit

Date: 2026-11-13
Target: Android 9 / ARM32 / MT8163
Reference: `platform_hardware_libhardware` tag `android-9.0.0_r3`
Status: source-level contract audit; no target stream implementation enabled

## Scope

This audit covers the Android 9 `audio_stream`, `audio_stream_out` and
`audio_stream_in` callback tables used by the Primary HAL. It does not assume
that vendor-private callbacks or undocumented factory extensions are part of
the public AOSP ABI.

Pinned public header:
- https://raw.githubusercontent.com/aosp-mirror/platform_hardware_libhardware/android-9.0.0_r3/include/hardware/audio.h

The matching system audio header must also be taken from the target Android 9
platform tree. Do not build channel-mask logic from numeric bit tests or use a
newer header as a substitute for the target branch.

## Callback contract

### Common stream

| Callback | Contract relevant to the wrapper |
|---|---|
| `get_sample_rate` | Return the currently negotiated rate, not a guessed default. |
| `set_sample_rate` / `set_format` / `set_device` | Marked unused by the public interface; standard parameter keys are used instead. Reject unsupported changes explicitly. |
| `get_buffer_size` | Return bytes; the size must be a multiple of the negotiated frame size. |
| `get_channels` | Return the direction-correct Android channel mask. |
| `get_format` | Return the negotiated Android format. |
| `standby` | Return 0 on success or a negative status. The next I/O operation must leave standby. |
| `dump` | Must not assume a valid fd; diagnostics must not mutate stream state. |
| `set_parameters` | Parse standard key/value pairs. If a parameter is valid but cannot be changed while active, the framework contract specifies `-ENOSYS` so AudioFlinger can retry after standby. |
| `get_parameters` | Return a heap-allocated NUL-terminated string; the caller frees it with `free()`. |
| `add_audio_effect` / `remove_audio_effect` | Effect ownership and lifetime must be explicit; do not silently claim support. |

### Output stream

- `write(stream, buffer, bytes)` returns a byte count or a negative status.
  If some complete frames were transferred before an error, returning that
  short byte count and reporting the error on a subsequent call is the
  documented preferred behavior.
- Without `set_callback`, the write path follows the synchronous blocking
  contract. After `set_callback` enables non-blocking mode, subsequent writes
  and drains must not block and completion must be reported through the
  callback.
- `set_volume`, render position and next-write timestamp must report only
  capabilities and measurements the backend actually implements.
- Pause/resume/drain/flush have ordering requirements. In particular, flush
  requires a paused stream; drain must complete promptly when stop/flush is
  requested.
- Presentation position is a frame count paired with the corresponding
  `CLOCK_MONOTONIC` timestamp. Do not synthesize a counter or timestamp.
- mmap callbacks are for mmap streams only. A non-mmap stream must reject
  mmap-only calls with the documented unsupported result.
- Source metadata update is a notification, not proof that the PCM transport
  supports offload or metadata processing.

### Input stream

- `read(stream, buffer, bytes)` returns a byte count or a negative status.
  A short successful read must be returned before surfacing a later error.
- `get_input_frames_lost` is a frame count, not a byte count.
- `get_capture_position` reports a monotonic frame count and its associated
  time; unsupported measurements must not be fabricated.
- mmap and active-microphone callbacks are capability-specific. They must not
  be populated with placeholder success behavior on a conventional PCM stream.
- Sink metadata update does not itself provide a capture transport.

## Byte/frame boundary

The current core API is intentionally frame-based:
- `PcmBackend::write/read` receive interleaved `int16_t` samples and a frame
  count.
- `PcmConfig` currently supports PCM16 LE and one or two channels.
- The Android HAL callbacks receive a byte count and Android format/channel
  mask.

The new `PcmByteStreamAdapter` derives bytes per frame from its validated
PCM16 `PcmConfig` and fixed direction. It rejects:
- null buffers;
- zero or non-frame-aligned byte counts;
- unsupported formats or channel counts;
- unaligned PCM16 buffers;
- frame counts exceeding the 2048-frame broker limit.

The adapter is deliberately PCM16-only and currently relies on the backend's
all-frames-or-error contract. It does not implement partial transfers or
deferred error reporting. The Android direction-specific channel-mask
conversion and stream callback tables remain separate integration work.

The current `PcmBackend` result type reports success/failure for a requested
frame count; it cannot represent a partial transfer. Before wiring it to
Android `write/read`, either the platform PCM operation must guarantee
all-requested-frames-or-error, or the backend contract must be extended to
report the actual transferred frame count. Do not return the requested byte
count after an unverified partial transfer.

## Findings against the current repository

1. `primary_hal.cpp` currently returns `-ENODEV` from both stream-open
   callbacks and clears the output stream pointer. This is the correct
   fail-closed behavior while no verified endpoint/backend exists.
2. `PrimaryHalStreamAdapter` is the C++ lifecycle bridge.
   `PcmByteStreamAdapter` now adds a per-stream PCM16 byte/frame conversion
   layer with callback admission and stop/close draining. Neither class owns
   the Android `audio_stream_out` / `audio_stream_in` callback tables or
   converts Android channel masks; partial transfers remain unsupported.
3. `PcmStreamBroker` admits frame-based transfers and protects stop against
   in-flight I/O. Its lifecycle contract requires a serialized control
   context. The HAL must additionally prevent device/adapter destruction
   while stream callbacks can still execute.
4. `EndpointPcmBackend` serializes overlapping transfers per backend stream,
   but it is not a concrete ALSA/tinyalsa implementation and has no verified
   endpoint map.
5. The recovered factory input-mask predicate in
   `mt8163_factory_stream_contract.h` accepts any mask containing the mono
   bit, including masks with additional input-channel bits. Those bits may
   describe a multichannel layout under Android semantics, so this is not a
   confirmed defect without matching policy/call-site evidence. The current
   `PcmConfig` is narrower: it supports only one/two-channel PCM16.

## Safe wrapper ownership design

When the backend and private ABI are verified:

- Allocate one stream wrapper per successful open in the HAL control context.
- Keep `audio_stream_out` or `audio_stream_in` as the first member of its
  wrapper so framework casts remain valid.
- Store an owning device-context pointer and a generated nonzero 32-bit stream
  handle in the wrapper; never truncate a pointer into the handle.
- Keep the stream adapter/backend owned by a per-device context. Do not use a
  process-global singleton.
- Preserve the recovered 224-byte `FactoryPrimaryDeviceLayout` prefix and
  offsets. Do not repurpose its private callback slots or legacy pointer as
  application storage until their vendor call semantics are independently
  recovered.
- Close streams before destroying the device context. Device close must not
  free the owner while any stream callback can still run.
- Keep unsupported optional capabilities explicit (`-ENOSYS` where the
  public contract defines it); do not install no-op callbacks that claim
  successful hardware behavior.

## Integration gates

Do not implement or enable stream open callbacks until all of the following
are true:

1. The exact target Android 9 `system/audio.h` and `hardware/audio.h`
   revisions are pinned and used by the ARM32 build.
2. Stream format, rate, channel-mask and buffer-size negotiation is checked
   against the target policy/profile and the verified PCM endpoint capability.
3. The backend transfer contract can represent Android's byte-count and
   partial-transfer semantics, or proves all-or-error behavior.
4. Device and stream lifetime is safe under concurrent read/write and
   serialized close/standby/stop operations.
5. Tests cover malformed configs, overflow, non-frame-aligned byte counts,
   partial I/O, backend errors, concurrent transfer/shutdown and slot reuse.
6. The endpoint map is derived from target runtime evidence; no card/device
   indices are inferred from factory names or enum values.

Host tests and ARM32 compilation do not establish target hardware behavior.
