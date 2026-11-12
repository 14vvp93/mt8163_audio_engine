# Endpoint PCM backend validation

## Host test target

`mt8163_audio_core_endpoint_backend_tests` is part of the normal AudioCore
CTest suite. It covers:

- disabled endpoint rejection and exact route-to-card/device dispatch;
- valid configure/start/write/stop/close lifecycle;
- rejection of four-channel configurations until target evidence supports them;
- callback frame-count bounds;
- overlapping writes on the same stream returning `WouldBlock`;
- XRUN propagation and transition to a failed state;
- duplicate route and non-PCM endpoint-table rejection.

The endpoint backend uses atomic state publication and a per-stream atomic
write gate because PCM callbacks may arrive concurrently. Lifecycle
operations remain serialized by the control plane; the PcmStreamBroker closes
callback admission and drains active writes before stop/close.

## Android build

The existing Android ARMv7 workflow compiles the AudioCore static library
with the new endpoint backend included. This validates the ARM32 compiler and
ABI for this library, not the complete Android 9 primary HAL or a device PCM
driver.

## Target evidence required before enabling endpoints

Do not enable any endpoint by default. Collect read-only target evidence for:

- `/proc/asound/cards` and `/proc/asound/pcm`;
- PCM playback/capture direction and substream ownership;
- mixer controls and selected Audio Policy configuration;
- process file descriptors for audio-related devices;
- supported sample formats, channel counts, rates, period and buffer sizes.

Only then add a target-specific endpoint profile and implement the
`PcmDeviceOps` bridge against the actual Android 9 tinyalsa/platform API.

No live PCM stream or hardware control operation is performed by the host
backend tests.
