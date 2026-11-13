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

## Pinned AOSP Android 9 baseline and close sequencing

The public AOSP reference is pinned to `platform/frameworks/av` tag
`android-9.0.0_r3`, commit
`8d3bfcbbd96603ad74df5ee003bbf100d74a1096` (PPR1.180610.011).
This matches the `platform_hardware_libhardware` tag used for the ABI
assertions. It is a reproducible upstream baseline, **not proof** that the
MT8163 vendor image uses this exact revision: the target build fingerprint,
repo manifest, and vendor cherry-picks are still absent.

### Output close

In this exact tag, `AudioFlinger::closeOutput_nonvirtual()`:
1. Holds a strong reference to the playback thread.
2. Removes the thread from `mPlaybackThreads` while holding AudioFlinger's
   `mLock`.
3. Releases `mLock`, calls `playbackThread->exit()`, then calls
   `closeOutputFinish()`, which clears and deletes the `AudioStreamOut`.

`ThreadBase::exit()` calls `preExit()` before requesting exit and waiting
with `requestExitAndWait()`. `PlaybackThread::preExit()` synchronously calls
the HAL `set_parameters("exiting=1")`; this is a real callback from the close
caller and must return before the worker shutdown begins.

The main PlaybackThread loop finishes its current iteration, runs
`threadLoop_exit()`, and if it is not already in standby calls
`threadLoop_standby()` before returning. For MixerThread,
`threadLoop_standby()` changes FastMixer to `COLD_IDLE` and pushes the state
queue with `BLOCK_UNTIL_ACKED`. The source comment explicitly says
`BLOCK_UNTIL_PUSHED` would not be enough because FastMixer must stop doing
I/O now. Thus FastMixer's HAL-facing output sink is quiescent before
`requestExitAndWait()` returns and before AudioFlinger deletes the stream.
The FastMixer object itself is only sent `EXIT` and joined later in
`MixerThread::~MixerThread()`; that later destruction is not the quiescence
point—the acknowledged `COLD_IDLE` transition is.

### Input close

In this exact tag, `AudioFlinger::closeInput_nonvirtual()` removes the
RecordThread from `mRecordThreads` under `mLock`, then
`closeInputFinish()` calls `thread->exit()`, clears the input and deletes
the `AudioStreamIn`.

When the RecordThread enters standby, `inputStandBy()` changes FastCapture
to `COLD_IDLE` and waits with `BLOCK_UNTIL_ACKED` before calling HAL
`standby()`. The RecordThread loop also calls
`standbyIfNotAlreadyInStandby()` before returning. Consequently the
FastCapture HAL read path is quiescent before the joined RecordThread exits
and before AudioFlinger deletes the input stream. The FastCapture object is
sent `EXIT` and joined later by `RecordThread::~RecordThread()`; as with
FastMixer, the acknowledged idle transition is the earlier I/O quiescence
point.

### Control-plane callback serialization

The exact Android 9.0.0_r3 source exposes a separate concurrency concern that
does not invalidate the worker I/O quiescence result:

- `AudioFlinger::getParameters()` holds AudioFlinger's `mLock` while it calls
  the selected thread's `getParameters()`. Close cannot remove that thread
  from the registry until this call returns.
- `AudioFlinger::setParameters()` obtains a strong `ThreadBase` reference
  under `mLock`, releases `mLock`, then calls `thread->setParameters()`.
  `ThreadBase::setParameters()` queues a config event; the worker executes
  `checkForNewParameter_l()` under its thread lock.
- Output close calls `PlaybackThread::preExit()` before taking the thread
  lock to request worker exit. `preExit()` directly invokes the HAL
  `set_parameters("exiting=1")` callback without acquiring that thread lock.

Therefore a previously admitted `setParameters()` operation can be in flight
while close runs `preExit()`. The worker's event path and the direct
`preExit()` callback are not proven mutually exclusive by these locks.
`requestExitAndWait()` subsequently waits for the worker before HAL stream
deletion, so the stream storage remains alive for worker-side callbacks, but
the two `set_parameters` invocations may overlap. The replacement HAL must
serialize stream control callbacks internally (without putting blocking
serialization on the realtime read/write path), or the exact vendor framework
must be shown to provide an additional serialization guarantee. Do not
mistake read/write quiescence for full callback-table serialization.

### Offload async callback thread and mmap

`OffloadThread::threadLoop_exit()` performs its pending flush/drain work
before the parent PlaybackThread exits, then calls
`AsyncCallbackThread::exit()`. That method only requests exit and signals
its condition; it does not synchronously join the callback thread. The
callback thread promotes a weak PlaybackThread reference and calls
`resetWriteBlocked()`, `resetDraining()`, or `onAsyncError()`; in this
source, `onAsyncError()` only invalidates tracks. This is not another
HAL-stream `write/read` dispatch path, but the async callback worker can
outlive the parent's I/O quiescence point briefly. Do not assume it has
terminated merely because `PlaybackThread::exit()` returned.

Mmap streams use separate `MmapPlaybackThread` /
`MmapCaptureThread` close branches and are not covered by the ordinary
PlaybackThread/RecordThread argument. The current HAL does not implement
mmap streams and must continue rejecting them.

### Current conclusion and provenance limit

For the pinned AOSP Android 9.0.0_r3 baseline, normal non-mmap PCM read/write
dispatch—including the auxiliary FastMixer and FastCapture workers—is
quiescent before the corresponding HAL close callback begins. The output
`preExit()` parameter callback is also complete before that point.
This removes the generic pre-admission UAF concern for callbacks exclusively
dispatched by those framework-owned workers **when the target framework
matches this baseline**.

This does not establish that every vendor-private callback, effect callback,
mmap callback, or independently dispatched control call is quiescent. Nor
does the upstream tag prove the target firmware has no vendor changes.
Keep the stream-open implementation fail-closed until target provenance and
the remaining callback classes are checked.

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
