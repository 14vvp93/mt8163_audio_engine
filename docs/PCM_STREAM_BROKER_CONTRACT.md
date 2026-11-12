# PCM backend and stream broker contract

Date: 2026-11-12  
Branch: `work/pcm-backend`  
Status: host-test candidate; CI required for the callback-admission update; no ALSA/device backend

## Components

- `PcmBackend`: transport interface. It intentionally contains no ALSA card/device indices.
- `PcmStreamBroker`: fixed-capacity adapter for at most 16 logical PCM streams.
- `PcmConfig`: sample rate, one/two channel count, period frame count and PCM16 LE format.
- `PcmResult`: explicit invalid-state, route, capacity, backpressure, xrun and device-loss outcomes.

## Lifecycle

The supported sequence is:

`open -> configure -> start -> write* -> stop -> close`

- `open` accepts only source/route pairs validated by the evidence-gated route policy and PCM transport routes.
- `configure` accepts only PCM16 with one or two channels and nonzero rate/period. It does not claim every rate is supported by every endpoint; the future platform backend must validate its recovered endpoint capabilities.
- `start` requires a configured stream and opens callback admission only after the backend starts successfully.
- `write` requires a started stream, non-null interleaved buffer and 1..2048 frames per call.
- `stop` atomically closes callback admission. If an already admitted backend write remains in flight, it returns `WouldBlock`; the control owner retries after the callback returns. The backend stop operation is not called until the in-flight count reaches zero.
- `close` refuses a started stream and checks that no broker-admitted write remains in flight.
- Capacity is fixed at 16; exhaustion is reported as `CapacityExceeded`.

## Realtime boundary and shutdown protocol

Each slot has a lock-free 32-bit admission word and a 32-bit generation counter. The admission word's high bit means callback admission is open and its low 31 bits count admitted writers. The generation increments whenever a closed slot is opened again; callbacks capture it before matching the handle and verify it again after acquiring admission, rejecting callbacks delayed across slot reuse. A callback uses compare/exchange to acquire a writer count only while the gate is open, calls the bounded backend write, then decrements the count. No allocation, mutex, condition variable or blocking wait is introduced in the callback path.

The control thread closes the gate with an atomic bit operation. This linearizes the race: a callback either increments the count before gate closure and is drained, or observes the closed gate and never enters the backend. `stop` returns `WouldBlock` while the count is nonzero; retrying after the writer exits performs the backend stop. The callback is not required to be externally joined before this broker-level stop. The generation check also rejects a callback invocation that captured the prior slot generation before close/reopen. The HAL must still prevent use-after-destruction of the broker itself and must serialize lifecycle calls.

The backend's `write` must be nonblocking/bounded and must not perform device discovery, recovery, synchronous logging or allocation. It reports `WouldBlock`, `Xrun` or `DeviceLost` to the caller; recovery belongs to the control plane.

## Explicit non-goals

- No production ALSA implementation.
- No guessed card/device/PCM indices.
- No automatic XRUN recovery in the callback.
- No four-channel PCM; the recovered MTK normal speaker boundary is stereo/mono-folded.
- No direct I2S or CD3313 analog input routed through this PCM interface.
- No kernel module, CD3313 ioctl, or hardware command.

## Validation required next

1. Host CI for broker lifecycle, capacity, invalid order, error propagation, concurrent slot opening, and stop racing an in-flight callback, and delayed callback rejection after slot reuse.
2. ARMv7 compile using the Android NDK.
3. Add endpoint capability negotiation after exact ALSA runtime topology is collected.
4. Add the HAL owner/lifetime protocol before wiring the broker to real streams.
5. Add a production ALSA backend only after Android 9 libasound/tinyalsa usage and device endpoints are verified.

A passing host test is not Android HAL or device validation.
