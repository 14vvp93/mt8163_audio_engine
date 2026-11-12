# Endpoint PCM backend boundary

## Implemented

`EndpointPcmBackend` is a concrete, fixed-capacity, direction-aware
implementation of the `PcmBackend` contract. It provides:

- explicit route + playback/capture direction to card/device endpoint mapping;
- primary microphone PCM capture and independent HFP/SCO playback/capture
  endpoints;
- capture-only FM PCM route validation, with FM direct I2S kept separate;
- rejection of disabled, duplicate and non-PCM route/direction pairs;
- opaque stream tokens independent of native PCM handles;
- validated Open → Configured → Started → Stopped → Closed lifecycle;
- bounded PCM16 playback writes and capture reads;
- lock-free per-stream transfer admission: overlapping callbacks return
  `WouldBlock` instead of concurrently entering the platform PCM API;
- XRUN and device-loss propagation without recovery or allocation in the
  callback path;
- injectable platform operations so host tests can verify route mapping,
  direction, state transitions and error handling.

## Deliberately not guessed

The adapter does not ship with enabled MT8163 card/device mappings. Static
firmware evidence does not establish live ALSA card indices, PCM device
indices, direction-specific substream ownership or runtime availability.
These must be collected from the target device.

The platform layer must supply `PcmEndpoint` records only after read-only
runtime evidence identifies each endpoint. A tinyalsa or other platform
adapter can implement `PcmDeviceOps`. Lifecycle operations are serialized
by the control plane; each stream admits one platform read or write at a time.
Callbacks must be safe across independent handles and bounded/nonblocking,
without allocation or logging. The current host fake is a test double, not
a hardware driver.

## Integration gates

1. Collect `/proc/asound/cards`, `/proc/asound/pcm`, PCM substream status,
   relevant sysfs nodes, mixer controls and process file descriptors.
2. Match primary playback/capture, A2DP playback, HFP/SCO downlink/uplink and
   FM capture to observed endpoint/direction pairs. Keep FM direct I2S separate.
3. Confirm PCM format, channels, rate, period/buffer constraints from the
   vendor HAL and live runtime.
4. Implement the target-specific `PcmDeviceOps` bridge against the exact
   Android 9 platform headers/library available in the product build.
5. Run host tests, Android ARMv7 compile, then read-only device validation
   before enabling any endpoint or connecting it to the factory HAL.

No live ALSA stream was opened and no hardware state was changed by this
implementation.
