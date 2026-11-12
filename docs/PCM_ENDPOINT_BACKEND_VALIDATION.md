# Endpoint PCM backend validation

## Host test targets

The normal AudioCore CTest suite includes:

- `mt8163_audio_core_endpoint_backend_tests`: route/direction mapping,
  playback and capture lifecycle, HFP downlink/uplink separation, overlapping
  transfer backpressure, XRUN recovery boundary and invalid endpoint tables.
- `mt8163_audio_core_pcm_stream_tests`: broker lifecycle, direction
  enforcement, capture reads, HFP bidirectional streams, slot capacity and
  stop/drain behavior under an in-flight transfer.
- `mt8163_audio_core_pcm_contract_tests`: route/direction invariants and
  PCM16 configuration validation.

The endpoint backend uses atomic state publication and a per-stream atomic
transfer gate because callbacks may arrive concurrently. Lifecycle
operations remain serialized by the control plane; PcmStreamBroker closes
transfer admission and drains active reads/writes before stop/close.

## Android build

The existing Android ARMv7 workflow compiles the AudioCore static library
with the endpoint backend included. This validates the ARM32 compiler and
ABI for this library, not the complete Android 9 primary HAL or a device PCM
driver.

## Target evidence required before enabling endpoints

Do not enable any endpoint by default. Collect read-only target evidence for:

- `/proc/asound/cards` and `/proc/asound/pcm`;
- PCM playback/capture direction and substream ownership/status;
- mixer controls and selected Audio Policy configuration;
- process file descriptors for audio-related devices;
- supported sample formats, channel counts, rates, period and buffer sizes.

Only then add a target-specific endpoint profile and implement the
`PcmDeviceOps` bridge against the actual Android 9 tinyalsa/platform API.

No live PCM stream or hardware control operation is performed by the host
backend tests.
