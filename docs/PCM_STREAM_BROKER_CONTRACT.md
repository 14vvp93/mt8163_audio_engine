# PCM backend and stream broker contract

Date: 2026-11-12  
Branch: `work/pcm-backend`  
Status: host-test candidate; CI is running for the latest changes; no ALSA/device backend

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
- `start` requires a configured stream.
- `write` requires a started stream, non-null interleaved buffer and 1..2048 frames per call.
- `stop` is idempotent after a successful stop.
- `close` refuses an active stream. The HAL must first quiesce the callback and stop the stream.
- Capacity is fixed at 16; exhaustion is reported as `CapacityExceeded`.

## Realtime boundary

The broker performs a bounded fixed-array lookup and does not allocate or lock. The backend's `write` must be nonblocking/bounded and must not perform device discovery, recovery, synchronous logging or allocation. It reports `WouldBlock`, `Xrun` or `DeviceLost` to the caller; recovery belongs to the control plane.

Lifecycle operations are serialized by the HAL control owner. The HAL must quiesce the audio callback before changing or destroying a stream record. Concurrent lifecycle calls and callback use without that quiescence contract are not supported.

## Explicit non-goals

- No production ALSA implementation.
- No guessed card/device/PCM indices.
- No automatic XRUN recovery in the callback.
- No four-channel PCM; the recovered MTK normal speaker boundary is stereo/mono-folded.
- No direct I2S or CD3313 analog input routed through this PCM interface.
- No kernel module, CD3313 ioctl, or hardware command.

## Validation required next

1. Host CI for broker lifecycle, capacity, invalid order and error propagation.
2. ARMv7 compile using the Android NDK.
3. Add endpoint capability negotiation after exact ALSA runtime topology is collected.
4. Add the HAL callback-quiescence/lifetime owner before wiring the broker to real streams.
5. Add a production ALSA backend only after Android 9 libasound/tinyalsa usage and device endpoints are verified.

A passing host test is not Android HAL or device validation.
