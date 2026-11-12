# AudioPolicy configuration audit

Date: 2026-11-12  
Inputs: vendor/system `audio_policy_configuration.xml` and vendor `audio_policy.conf`.

## 1. Bluetooth SCO microphone profile mismatch

In `files2/files2/vendor/audio_policy_configuration.xml`, device port `BT SCO Headset Mic` has role `source` (input), but its profile declares:

- sample rates: 8,000 and 16,000 Hz;
- channel mask: `AUDIO_CHANNEL_OUT_STEREO`.

The corresponding entry in `files2/files2/system/audio_policy_configuration.xml` is correctly configured as an input and uses `AUDIO_CHANNEL_IN_MONO`.

This is a concrete vendor XML inconsistency. If the vendor XML is the active configuration, SCO microphone profile negotiation can be wrong or rejected. The system XML provides a known-correct comparison.

## 2. Missing include in the supplied snapshot

Both vendor and system XML files include:

- `a2dp_audio_policy_configuration.xml`;
- `usb_audio_policy_configuration.xml`;
- `r_submix_audio_policy_configuration.xml`;
- `audio_policy_volumes.xml`;
- `default_volume_tables.xml`.

The first four referenced files are present in the extracted archive; `default_volume_tables.xml` is not present anywhere in the extracted tree.

This is not yet proof of a factory firmware defect: the `files2.zip` archive may be an incomplete partition subset. If the include is also absent from the live system, XML configuration loading may fail or fall back to another policy configuration. Verify the full `/vendor/etc` and `/system/etc` directories before changing it.

## 3. Legacy policy configuration

The supplied vendor `audio_policy.conf` defines the primary input profile with `AUDIO_CHANNEL_IN_MONO` among its supported masks and includes `AUDIO_DEVICE_IN_ALL_SCO` in its device list. This is consistent with a mono SCO microphone, unlike the vendor XML's output mask.

## 4. Recommended correction

1. Correct the vendor SCO mic profile to `AUDIO_CHANNEL_IN_MONO`, preserving its supported sample rates.
2. Confirm which policy XML is selected at boot and whether the missing volume-table include exists on the full image.
3. Run AudioPolicy parsing and Bluetooth SCO input tests after the full config set is available.

Static configuration analysis only; no live audio policy was changed or tested.
