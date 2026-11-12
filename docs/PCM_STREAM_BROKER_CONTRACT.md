# PCM backend and stream broker contract

Date: 2026-11-12  
Branch: `work/pcm-backend`  
Status: bidirectional host-test candidate; no target ALSA binding

## Components

- `PcmBackend`: direction-aware transport interface. It intentionally contains no ALSA card/device indices.
- `PcmStreamBroker`: fixed-capacity adapter for at most 16 logical PCM streams.
- `PcmDirection`: separates playback writes from capture reads; HFP/SCO has independent uplink and downlink streams.
- `PcmConfig`: sample rate, one/two channel count, period frame count and PCM16 LE format.
- `PcmResult`: explicit invalid-state, route, capacity, backpressure, xrun and device-loss outcomes.

## Lifecycle

Playback sequence:

`open(route, Playback) -> configure -> start -> write* -> stop -> close`

Capture sequence:

`open(route, Capture) -> configure -> start -> read* -> stop -> close`

- `open` accepts only source/route pairs validated by the evidence-gated route policy and valid route/direction combinations.
- Primary PCM, A2DP and HDMI are playback routes. HFP/SCO supports separate playback and capture endpoints. FM capture is capture-only. FM direct I2S and CD3313 analog inputs remain outside this PCM transport.
- `configure` accepts only PCM16 with one or two channels and nonzero rate/period. It does not claim every rate is supported by every endpoint; the future platform backend must validate recovered endpoint capabilities.
- `start` requires a configured stream and opens transfer admission only after the backend starts successfully.
- `write` is valid only for playback streams; `read` is valid only for capture streams. Both require a started stream, non-null interleaved buffer and 1..2048 frames per call.
- `stop` atomically closes transfer admission. If an already admitted read or write remains in flight, it returns `WouldBlock`; the control owner retries after the callback returns. The backend stop operation is not called until the in-flight count reaches zero.
- `close` refuses a started stream and checks that no broker-admitted transfer remains in flight.
- Capacity is fixed at 16; exhaustion is reported as `CapacityExceeded`.

## Realtime boundary and shutdown protocol

Each slot has a lock-free 32-bit admission word and a 32-bit generation counter. The admission word's high bit means transfer admission is open and its low 31 bits count admitted readers/writers. The generation increments whenever a closed slot is opened again; callbacks capture it before matching the handle and verify it again after acquiring admission, rejecting callbacks delayed across slot reuse. A callback uses compare/exchange to acquire a transfer count only while the gate is open, calls the bounded backend read/write, then decrements the count. No allocation, mutex, condition variable or blocking wait is introduced in the callback path.

The control thread closes the gate with an atomic bit operation. This linearizes the race: a callback either increments the count before gate closure and is drained, or observes the closed gate and never enters the backend. `stop` returns `WouldBlock` while the count is nonzero; retrying after all transfers exit performs the backend stop. The HAL must still prevent use-after-destruction of the broker itself and serialize lifecycle calls.

The endpoint backend adds a per-stream atomic transfer gate so overlapping playback writes or capture reads return `WouldBlock` rather than entering the platform PCM API concurrently. Its platform callbacks must be bounded, nonblocking and allocation/logging-free.

## Explicit non-goals

- No production tinyalsa/ALSA binding or enabled target endpoint map.
- No guessed card/device/PCM indices.
- No automatic XRUN recovery in the callback.
- No four-channel PCM; the recovered MTK normal speaker boundary is stereo/mono-folded.
- No direct I2S or CD3313 analog input routed through this PCM interface.
- No kernel module, CD3313 ioctl, or hardware command.

## Validation required next

1. Host CI for playback/capture lifecycle, HFP bidirectional endpoints, capacity, invalid order, error propagation, concurrent slot opening, and stop racing an in-flight transfer.
2. ARMv7 compile using the Android NDK.
3. Add endpoint capability negotiation after exact ALSA runtime topology is collected.
4. Add the HAL owner/lifetime protocol before wiring the broker to real streams.
5. Add a production tinyalsa/ALSA adapter only after Android 9 platform API and target endpoints are verified.

A passing host test is not Android HAL or device validation.
