# PCM backend and stream broker contract

Date: 2026-11-13  
Branch: `feature`  
Status: bidirectional host-test candidate; no target ALSA binding

## Components

- `PcmBackend`: direction-aware transport interface. It intentionally contains no ALSA card/device indices.
- `PcmStreamBroker`: fixed-capacity adapter for at most 16 logical PCM streams.
- `PcmDirection`: separates playback writes from capture reads; HFP/SCO has independent uplink and downlink streams.
- `PcmConfig`: sample rate, one/two channel count, period frame count and PCM16 LE format.
- `PcmResult`: explicit invalid-state, route, capacity, backpressure, xrun and device-loss outcomes.
- `PcmTransferResult`: reports both the transfer status and the exact number of completed frames, including partial transfers.

## Lifecycle

Playback sequence:

`open(route, Playback) -> configure -> start -> write* -> stop -> close`

Capture sequence:

`open(route, Capture) -> configure -> start -> read* -> stop -> close`

- `open` accepts only source/route pairs validated by the evidence-gated route policy and valid route/direction combinations.
- Primary PCM and A2DP are playback routes. Primary microphone PCM and FM capture are capture-only. HFP/SCO supports independent playback and capture endpoints. HDMI is playback-only. FM direct I2S and CD3313 analog inputs remain outside this PCM transport.
- `configure` accepts only PCM16 with one or two channels and nonzero rate/period. It does not claim every rate is supported by every endpoint; the future platform backend must validate recovered endpoint capabilities.
- `start` requires a configured stream and opens transfer admission only after the backend starts successfully.
- `write` is valid only for playback streams; `read` is valid only for capture streams. Both require a started stream, non-null interleaved buffer and 1..2048 frames per call.
- `stop` atomically closes transfer admission. If an already admitted read or write remains in flight, it returns `WouldBlock`; the control owner retries after the callback returns. The backend stop operation is not called until the in-flight count reaches zero.
- `Xrun`, `DeviceLost`, and malformed transfer counts transition the broker stream to `Failed` and close callback admission. The backend stop requirement remains set until an explicit successful `stop`; `close` must not bypass that cleanup.
- `close` refuses a started stream and checks that no broker-admitted transfer remains in flight.
- Capacity is fixed at 16; exhaustion is reported as `CapacityExceeded`.

## Realtime boundary and shutdown protocol

Each slot has a lock-free 32-bit admission word and a 32-bit generation counter. The admission word's high bit means transfer admission is open and its low 31 bits count admitted readers/writers. The generation increments whenever a closed slot is opened again; callbacks capture it before matching the handle and verify it again after acquiring admission, rejecting callbacks delayed across slot reuse. A callback uses compare/exchange to acquire a transfer count only while the gate is open, calls the bounded backend read/write, then decrements the count. No allocation, mutex, condition variable or blocking wait is introduced in the callback path.

The control thread closes the gate with an atomic bit operation. This linearizes the race: a callback either increments the count before gate closure and is drained, or observes the closed gate and never enters the backend. `stop` returns `WouldBlock` while the count is nonzero; retrying after all transfers exit performs the backend stop. The HAL must still prevent use-after-destruction of the broker itself and serialize lifecycle calls.

The endpoint backend adds a per-stream atomic transfer gate so overlapping playback writes or capture reads return `WouldBlock` rather than entering the platform PCM API concurrently. Its platform callbacks must be bounded, nonblocking and allocation/logging-free.

## Android HAL byte/frame bridge

`PcmByteStreamAdapter` is a per-stream wrapper around
`PrimaryHalStreamAdapter`. It owns the negotiated `PcmConfig` snapshot and
direction so individual realtime calls cannot supply a different channel
count or format.

- Android-facing byte counts are validated against PCM16 bytes per frame.
- Null, zero-length, misaligned buffers, non-frame-aligned byte counts and
  transfers above 2048 frames are rejected before entering the broker.
- Playback and capture methods are direction-specific.
- A successful backend call maps to the complete requested byte count; an
  error maps to zero transferred bytes.
- This relies on the current all-frames-or-error `PcmBackend` contract. It
  does not yet model partial transfers or deferred errors, so it is not by
  itself a complete Android `audio_stream_out::write` /
  `audio_stream_in::read` implementation.
- The wrapper must be destroyed only after explicit successful close, and its
  referenced `PrimaryHalStreamAdapter` must outlive it.
- The admission gate protects configuration/backend access after a callback
  enters the gate. It cannot keep the C++ wrapper object alive for a callback
  that has begun executing but has not yet acquired admission. The HAL owner
  must quiesce callback dispatch and serialize stream destruction externally.

The Android HAL device callbacks remain fail-closed. This bridge does not
enable an output/input stream or select a hardware endpoint.

## Explicit non-goals

- No production tinyalsa/ALSA binding or enabled target endpoint map.
- No guessed card/device/PCM indices.
- No automatic XRUN recovery in the callback.
- No four-channel PCM; the recovered MTK normal speaker boundary is stereo/mono-folded.
- No direct I2S or CD3313 analog input routed through this PCM interface.
- No kernel module, CD3313 ioctl, or hardware command.

## Validation required next

1. Host CI for playback/capture lifecycle, primary microphone capture, HFP downlink/uplink, capacity, invalid order, error propagation (including XRUN/device loss), concurrent slot opening, and stop racing an in-flight transfer.
2. ARMv7 compile using the Android NDK.
3. Add endpoint capability negotiation after exact ALSA runtime topology is collected.
4. Add the HAL owner/lifetime protocol before wiring the broker to real streams.
5. Add a production tinyalsa/ALSA adapter only after Android 9 platform API and target endpoints are verified.

A passing host test is not Android HAL or device validation.
