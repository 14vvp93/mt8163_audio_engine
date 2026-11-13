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
PCM16 `PcmConfig` and fixed direction. The backend transfer contract now
returns the actual number of frames transferred independently from the
status, allowing a short successful transfer or a short transfer followed by
an error to cross the byte adapter without claiming that the full request was
completed. Legacy status-only backends retain an explicit all-or-error
fallback. The adapter rejects:
- null buffers;
- zero or non-frame-aligned byte counts;
- unsupported formats or channel counts;
- unaligned PCM16 buffers;
- frame counts exceeding the 2048-frame broker limit.

The adapter is deliberately PCM16-only. It propagates partial byte counts
alongside the backend status. The eventual Android stream wrapper must return
a positive short byte count first and retain any accompanying error for the
next callback, as required by the Android blocking stream contract. The
direction-specific channel-mask conversion and stream callback tables remain
separate integration work.

The `PcmBackend` transfer result now carries both status and actual frames
transferred. Legacy implementations that only implement status-returning
`write/read` use a compatibility fallback that treats success as a complete
transfer. Platform adapters must use the extended callbacks whenever their
underlying API can report partial progress. A status-only legacy caller rejects
a short successful transfer rather than silently reporting it as complete.

## Findings against the current repository

1. `primary_hal.cpp` currently returns `-ENODEV` from both stream-open
   callbacks and clears the output stream pointer. This is the correct
   fail-closed behavior while no verified endpoint/backend exists.
2. `PrimaryHalStreamAdapter` is the C++ lifecycle bridge.
   `PcmByteStreamAdapter` adds a per-stream PCM16 byte/frame conversion layer
   with callback admission and stop/close draining. It now preserves exact
   partial frame counts and accompanying backend errors. Neither class owns
   the Android `audio_stream_out` / `audio_stream_in` callback tables or
   converts Android channel masks.
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

## Framework close sequencing: source-level evidence

The framework-side close path is materially stronger than the generic HAL
header contract:

- AudioFlinger's output close path removes the playback thread from its
  registry, calls `PlaybackThread::exit()`, then clears the stream and calls
  `close_output_stream()`. The source contains an explicit comment that the
  thread entity is no longer running after `exit()`.
- AudioFlinger's input close path follows the corresponding sequence:
  remove the record thread, call `RecordThread::exit()`, clear the input, then
  call `close_input_stream()`.
- Read/write callbacks are driven by those playback/record worker threads.
  Therefore, if the target branch's `exit()` synchronously joins the worker
  and no other framework path invokes I/O callbacks, the worker-originated
  read/write dispatch has quiesced before the HAL close callback starts.

Source references inspected for this sequencing (the exact Android 9 tag
must still be pinned in the target platform checkout):

- https://android.googlesource.com/platform/frameworks/av/+/2e422c4/services/audioflinger/AudioFlinger.cpp
  (output close path)
- https://android.googlesource.com/platform/frameworks/av/+/7fa0152/services/audioflinger/AudioFlinger.cpp
  (input close path)

These are source-level call-path references, not proof that the target vendor
image uses identical framework code. The repository's pinned
`platform_hardware_libhardware` tag does not pin
`platform_frameworks_av`, and the matching framework revision has not yet
been recovered from the target image.

Important scope limitation: this sequencing can justify wrapper reclamation
only for callbacks whose dispatch is exclusively owned by the stopped worker
thread. It does not automatically cover vendor-private callbacks, callbacks
from Binder/control threads, effect callbacks, mmap paths, or asynchronous
HAL-originated callback mechanisms. Those require separate call-site and
serialization audits. In particular, do not infer a global "no callback can
run after close" guarantee from the public `audio.h` declarations.

Next verification gate: pin the exact target `frameworks/av` revision and
inspect `PlaybackThread::exit()`, `RecordThread::exit()`, their thread-loop
shutdown/join behavior, and every call site of the stream callback tables.
Only then decide whether framework worker quiescence is sufficient for the
public read/write callbacks, or whether additional stable dispatch ownership
is required for any other callback class.

## Thread shutdown semantics: additional source evidence

The inspected AudioFlinger implementation does not merely signal worker shutdown:
`ThreadBase::exit()` calls `preExit()`, sets the exit request while waking the
worker condition variable, then calls `requestExitAndWait()`. The corresponding
`libutils::Thread::requestExitAndWait()` waits until the worker's
`mRunning` state becomes false. This establishes a synchronous worker
quiescence point in those inspected source revisions.

There is one important callback before that quiescence point:
`PlaybackThread::preExit()` invokes the output stream's
`common.set_parameters("exiting=1")`. It is part of the close caller's
shutdown sequence and must be treated as a live HAL callback until it returns.
The worker join only proves that worker-thread dispatch has stopped after
`exit()` returns; it does not prove that arbitrary control/API calls or
vendor-private callback mechanisms cannot race close.

Additional source references inspected:

- https://android.googlesource.com/platform/frameworks/av/+/45e3065/services/audioflinger/Threads.cpp
  (`ThreadBase::exit()` and its `requestExitAndWait()` call)
- https://android.googlesource.com/platform/frameworks/av/+/e93cf2ca27ae6f4a81d4ef548bbf10a34db6d98f/services/audioflinger/Threads.cpp
  (`PlaybackThread::preExit()` calling output `set_parameters`)
- https://android.googlesource.com/platform/system/core/+/d2baf64a1081d5f0cd44d694b24afaab6ff4f4a6/libutils/Threads.cpp
  (`Thread::requestExitAndWait()` waiting for `mRunning == false`)

These commits are source evidence for the shutdown mechanism, not a recovered
revision identity for the target vendor image. Do not promote this to a
target-specific guarantee until the target `frameworks/av` and
`system/core` revisions are pinned. The callback inventory must also cover
control methods (parameters, standby, effects and device controls), mmap
threads, and offload/async callback workers independently.

## Reproducible AOSP Android 9 baseline

The upstream Android 9.0.0 Release 3 tag has now been resolved for the two
framework repositories used by this lifecycle analysis:

- `platform/frameworks/av`, tag `android-9.0.0_r3`, commit
  `8d3bfcbbd96603ad74df5ee003bbf100d74a1096` (build
  `PPR1.180610.011`).
- `platform/system/core`, tag `android-9.0.0_r3`, commit
  `0fac9018f2388f25f5a5778663a14712e75a9903` (same build).

This pins a reproducible public AOSP baseline matching the
`platform_hardware_libhardware` tag already used for ABI assertions. It does
**not** prove that the MT8163 vendor image was built from these exact commits:
the target image fingerprint, repo manifest, and vendor cherry-picks are not
present in this repository. Treat the tag as the reference baseline, not as
recovered device provenance.

The `system/core` implementation is especially important: `Thread`'s
destructor is empty. The join guarantee comes from the explicit
`Thread::requestExitAndWait()` call in `ThreadBase::exit()`, which waits
until `mRunning` becomes false. It must not be attributed to object
destruction or to `requestExit()` alone.

The offload path needs separate treatment. `OffloadThread::threadLoop_exit()`
requests exit on `AsyncCallbackThread` when asynchronous writes are enabled,
but `AsyncCallbackThread::exit()` itself only sets the exit request and
signals its condition; it does not call `requestExitAndWait()`. Its worker
processes acknowledgements/errors back into the playback-thread object; it is
not the ordinary PCM `write()` dispatch path. Do not claim that the parent
thread's join also joins this callback thread. Any lifetime argument involving
the HAL's registered asynchronous callback must be audited against the
specific HAL callback registration/unregistration contract.

Mmap playback/capture uses separate AudioFlinger thread classes and close
branches. The ordinary PlaybackThread/RecordThread sequence cannot be applied
to mmap streams. This project currently has no mmap stream implementation,
and its HAL stream-open methods remain fail-closed.

For ordinary non-mmap PCM streams, the current defensible conclusion is
narrow: AudioFlinger removes the thread from its registry, exits and joins
the worker, then clears the stream and invokes the HAL close method. This is
a strong quiescence guarantee for I/O dispatched exclusively by that worker.
It is not a blanket guarantee for calls already holding a thread reference,
control-plane operations, effects, mmap, or vendor-private asynchronous
callbacks. Keep the stable-dispatch-lifetime question open until those
classes and the exact target vendor framework delta are checked.

## Callback dispatch lifetime: unresolved hard boundary

An atomic admission counter stored inside a stream wrapper is not a lifetime
owner. A callback can be dispatched with the raw `audio_stream_out*` or
`audio_stream_in*`, be descheduled before its first access to the wrapper,
and resume after the close path has observed zero in-flight operations. The
callback's first atomic access would then itself be a use-after-free.

Consequences:

- Do not free a stream wrapper from `close_output_stream` or
  `close_input_stream` merely because the adapter/broker transfer count is
  zero.
- Do not treat a gate embedded in the wrapper, a reference count incremented
  through that wrapper, or a generation field in that wrapper as protection
  for callback entry. Each requires dereferencing memory whose lifetime is
  the disputed property.
- A valid implementation needs a dispatch-lifetime mechanism whose storage
  outlives every possible callback entry. Callback admission must happen
  through that stable storage before the callback dereferences the stream
  wrapper. The device owner and backend must remain alive until all admitted
  callbacks have exited.
- The mechanism must also define stale callback behavior across stream close,
  address reuse, and device close. A raw stream pointer carries no generation
  token, so an internal generation counter alone cannot distinguish a stale
  callback from a new stream allocated at the same address.
- If the Android framework's close contract guarantees that no callback can
  begin or remain pending after close returns, pin and verify that exact
  framework contract for the target Android 9 branch. Otherwise retain a
  stable dispatch identity/tombstone or use another externally verified
  lifetime mechanism; do not guess.

The current repository has no such dispatch owner. The fail-closed stream-open
callbacks are therefore intentional. The broker and byte-adapter gates protect
already-admitted transfers only; they do not authorize wrapper destruction.

Current regression coverage in the repository:

- `PcmByteStreamAdapter` tests pause an admitted output transfer and verify
  stop/close return retryable `WouldBlock`, then succeed after the transfer
  exits.
- `PrimaryHalStreamAdapter` now has the corresponding capture-read race:
  close rejects while an admitted read is blocked, later reads are rejected
  after admission closes, and close succeeds after the read exits.
- `PcmStreamBroker` tests cover transfer admission shutdown, stop retry, and
  reopening the same logical handle while an earlier callback is active.

These tests exercise callbacks after they have entered the adapter/broker
admission gate. They do **not** reproduce a framework-dispatched raw callback
paused before its first access to the stream wrapper. That pre-admission
lifetime case still requires either verified framework-side quiescence or a
stable dispatch owner and remains a release gate.

Required deterministic tests before enabling stream callbacks:

1. Pause a callback after framework dispatch but before its first wrapper
   access; race stream close and verify wrapper/owner storage remains valid.
2. Pause a callback after successful dispatch admission; race close and verify
   close cannot reclaim its owner until the lease exits.
3. Start a callback after admission has been shut down; verify it is rejected
   without touching the wrapper or backend.
4. Race stream close, device close, and callback admission; verify backend and
   owner destruction occurs only after all admitted callbacks drain.
5. Reuse a stream slot and, separately, an allocation address; verify stale
   callback identity cannot resolve to a newly opened stream.
6. Run the above under ThreadSanitizer and the Android ARM32 ABI build.

Do not add an unbounded wait to a realtime callback. Waiting, if required by a
verified design, belongs to the serialized control/close context and must have
a bounded or retryable failure mode.

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
   partial I/O propagation, backend errors, concurrent transfer/shutdown and
   slot reuse. Android callback-level deferred-error tests remain outstanding.
6. The endpoint map is derived from target runtime evidence; no card/device
   indices are inferred from factory names or enum values.

Host tests and ARM32 compilation do not establish target hardware behavior.
