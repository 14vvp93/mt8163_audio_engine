# Endpoint PCM backend boundary

## Implemented

`EndpointPcmBackend` is a concrete, fixed-capacity implementation of the
`PcmBackend` contract. It provides:

- an explicit route-to-card/device endpoint table supplied by the platform;
- rejection of disabled, duplicate and non-PCM routes;
- opaque stream tokens independent of native PCM handles;
- validated Open → Configured → Started → Stopped → Closed lifecycle;
- bounded write dispatch with PCM16 frame validation;
- XRUN and device-loss propagation without recovery or allocation in the
  callback path;
- injectable platform operations so host tests can verify route mapping,
  state transitions and error handling.

## Deliberately not guessed

The adapter does not ship with enabled MT8163 card/device mappings. The
repository's static firmware evidence does not establish the live ALSA card
index, PCM device index, direction, active endpoint ownership or target
runtime availability. These must be collected from the target device.

The platform layer must supply `PcmEndpoint` records only after read-only
runtime evidence has identified each endpoint. A tinyalsa or other platform
adapter can implement `PcmDeviceOps`; it must guarantee nonblocking bounded
writes, per-handle concurrency safety, and no allocation/logging in the
callback. The current host fake is a test double, not a hardware driver.

## Integration gates

1. Collect `/proc/asound/cards`, `/proc/asound/pcm`, relevant sysfs nodes,
   mixer controls and process file descriptors on the actual head unit.
2. Match each proposed route (primary, A2DP, HFP/SCO, FM capture) to an
   observed endpoint and direction. Keep FM direct I2S separate.
3. Confirm PCM format, channels, rate, period/buffer constraints from the
   vendor HAL and live runtime.
4. Implement the target-specific `PcmDeviceOps` wrapper against the exact
   Android 9 platform headers/library available in the product build.
5. Run host tests, Android ARMv7 compile, then read-only device validation
   before enabling any endpoint or connecting it to the factory HAL.

No live ALSA stream was opened and no hardware state was changed by this
implementation.
