# MT8163 output-device dispatch audit

## Scope and source identity

This audit rechecks the output-device dispatch logic in the supplied
`vendor/audio.primary.mt8163.so` and compares it with the supplied
`vendor/audio_device.xml`.

- HAL SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- ELF Build ID: `f66eaf9e6ac950259f0c253be618d15a`
- The uploaded `extracted-files.zip` contains the same HAL binary.
- The supplied `audio_device.xml` SHA-256 is
  `eb3b490a033e26c00514c3a9b54f5ebf5bcc965e9839d746bc4f7a67bcaef56a`.

This is static analysis only. It does not establish which configuration file
is actually loaded on the running head unit.

## 1. Confirmed call from the Normal playback handler

The base playback-handler constructor at `0xD16C4` stores:
- the `AudioALSAHardwareResourceManager::getInstance()` result at handler
  offset `+0x08`;
- the `stream_attribute_t*` argument at handler offset `+0x0C`.

In `AudioALSAPlaybackHandlerNormal::open()` at `0xD3B58`, the sequence at
`0xD4098–0xD40AC`:
1. loads those two pointers from handler offsets `+0x08` and `+0x0C`;
2. loads the output-device field from `stream_attribute_t +0x18`;
3. loads the second argument from handler `+0x38` (the Normal handler's
   selected target sample rate);
4. loads the resource-manager virtual function at vtable offset `+0x10`;
5. invokes it.

The resource-manager vtable entry at `+0x10` resolves to
`AudioALSAHardwareResourceManager::startOutputDevice(unsigned int, unsigned int)`.
Therefore this Normal-handler call passes the output-device field unchanged
as the first argument and the selected target rate as the second argument.
This closes the upstream link from the stream attribute to the resource
manager's internal dispatch for the Normal playback path.

This does not establish that every playback handler uses the same route path.


The matching `AudioALSAPlaybackHandlerNormal::close()` path at `0xD4284`
loads the same resource-manager pointer from handler `+0x08`, obtains its
vtable entry at `+0x14`, and invokes it at `0xD42D8–0xD42E4`.
That entry resolves to `AudioALSAHardwareResourceManager::stopOutputDevice()`.
The manager then uses the stored selector to reach `stopOutputDevice_l()`.
Thus the Normal handler lifecycle is confirmed as:
`open() -> startOutputDevice(device_mask, target_rate)` and
`close() -> stopOutputDevice()`.

## 2. Start dispatch table and Android device mapping

`AudioALSAHardwareResourceManager::startOutputDevice_l(unsigned int, unsigned int)`
at `0x74374` stores the first argument as the current output selector,
subtracts one, bounds-checks it against 0..9, and dispatches through a
ten-entry jump table. The selected branch loads a callback from the resource
manager vtable and invokes it with the second argument.

The primary vtable address point is the one for which offset `+0x10` resolves
to `startOutputDevice()`. Resolving the packed ARM relocations against that
address point gives the following callback identities:

| Selector / device mask | Android route | Start vtable offset | Callback |
|---:|---|---:|---|
| 1 (`0x01`) | `AUDIO_DEVICE_OUT_EARPIECE` | +0xB0 | `OpenReceiverPath(unsigned int)` |
| 2 (`0x02`) | `AUDIO_DEVICE_OUT_SPEAKER` | +0xC0 | `OpenSpeakerPath(unsigned int)` |
| 4 (`0x04`) | `AUDIO_DEVICE_OUT_WIRED_HEADSET` | +0xB8 | `OpenHeadphonePath(unsigned int)` |
| 6 (`0x06`) | Speaker + wired headset | +0xC8 | `OpenHeadphoneSpeakerPath(unsigned int)` |
| 8 (`0x08`) | `AUDIO_DEVICE_OUT_WIRED_HEADPHONE` | +0xB8 | `OpenHeadphonePath(unsigned int)` |
| 10 (`0x0A`) | Speaker + wired headphone | +0xC8 | `OpenHeadphoneSpeakerPath(unsigned int)` |

Selectors 3, 5, 7 and 9 take the default/no-callback path in this switch.
The numeric mapping is consistent with the Android output-device bit values:
earpiece 0x01, speaker 0x02, wired headset 0x04 and wired headphone 0x08.
The combined values 0x06 and 0x0A select the combined headphone-speaker path.

This mapping is now grounded by both the Normal handler's direct argument
flow and the resource-manager's branch/vtable dispatch. It should not be
generalized to HDMI, FM transmitter, Bluetooth SCO, voice, or other handlers
without tracing those paths separately.

## 3. Stop dispatch table and verified open/close pairs

`AudioALSAHardwareResourceManager::stopOutputDevice_l()` at `0x74400`
uses a second ten-entry table. Calculating the branch targets from its table
base at `0x74428` and resolving the vtable relocations gives:

| Selector / device mask | Stop vtable offset | Callback |
|---:|---:|---|
| 1 (`0x01`) | +0xB4 | `CloseReceiverPath()` |
| 2 (`0x02`) | +0xC4 | `CloseSpeakerPath()` |
| 3 | — | default/no callback |
| 4 (`0x04`) | +0xBC | `CloseHeadphonePath()` |
| 5 | — | default/no callback |
| 6 (`0x06`) | +0xCC | `CloseHeadphoneSpeakerPath()` |
| 7 | — | default/no callback |
| 8 (`0x08`) | +0xBC | `CloseHeadphonePath()` |
| 9 | — | default/no callback |
| 10 (`0x0A`) | +0xCC | `CloseHeadphoneSpeakerPath()` |

The resolved vtable callback block is:

| Vtable byte offset | Method |
|---:|---|
| +0xB0 | `OpenReceiverPath(unsigned int)` |
| +0xB4 | `CloseReceiverPath()` |
| +0xB8 | `OpenHeadphonePath(unsigned int)` |
| +0xBC | `CloseHeadphonePath()` |
| +0xC0 | `OpenSpeakerPath(unsigned int)` |
| +0xC4 | `CloseSpeakerPath()` |
| +0xC8 | `OpenHeadphoneSpeakerPath(unsigned int)` |
| +0xCC | `CloseHeadphoneSpeakerPath()` |
| +0xD0 | `OpenBuiltInMicPath()` |
| +0xD4 | `CloseBuiltInMicPath()` |

This establishes consistent start/stop pairs for all six ordinary output
selectors listed above. It also corrects the earlier callback interpretation:
the previous table was based on an address point shifted by eight bytes.
The vtable entry at `+0x10` and the constructor/call-site flow fix that
address-point ambiguity.

## 4. Higher-level transition wrappers

The disassembly shows these wrapper behaviors:

- `startOutputDevice(device, mode)` compares the requested device with the
  currently stored device at object offset +0x20. On a route change it calls
  an indirect device-operation callback, increments the active-device count at
  +0x34, and performs additional state handling.
- `changeOutputDevice(device)` also uses indirect device-operation callbacks
  to perform a route transition. It is not equivalent to the simple
  `setOutputDevice()` state setter.
- `setOutputDevice(device, mode)` stores the device and mode at +0x20 and
  +0x28; it does not itself execute the named speaker mixer sequence.
- `OpenSpeakerPath()` calls the external-DAC GPIO enable helper and then
  `ApplyDeviceTurnonSequenceByName("speaker_output")`.
- `CloseSpeakerPath()` calls the matching turn-off sequence and disables the
  external-DAC GPIO, subject to its state guard.

## 5. Exact configuration-name comparison

The supplied `audio_device.xml` is well-formed XML and defines these output
paths:

- `receiver_output`
- `two_in_one_speaker_output`
- `headphoneSpeaker_output`

It does not define:

- `speaker_output`
- `headphone_output`

The HAL's `OpenSpeakerPath()` passes the exact literal
`speaker_output`; `OpenHeadphonePath()` passes `headphone_output`.
The XML's `two_in_one_speaker_output` and `headphoneSpeaker_output` are
different strings. The configuration manager performs exact path-name lookup,
so no automatic aliasing should be assumed.

The captured artifacts therefore contain a static HAL/XML name mismatch.
Possible explanations include a different live XML revision, an alternate
configuration source, or a packaging mismatch. The live configuration has
not been inspected, so a runtime failure is not claimed.

## 6. Consequences for DSP integration

1. The Normal playback path's ordinary speaker selector is confirmed as
   `AUDIO_DEVICE_OUT_SPEAKER (0x02)`, and it reaches `OpenSpeakerPath()`.
2. Do not change the XML names or patch the HAL based only on the captured
   HAL/XML pair; obtain the live unit's resolved device-path configuration
   and mixer controls first.
3. Keep the DSP outside this route-control layer. GPIO and mixer sequences
   are hardware-control operations, not PCM processing.
4. Do not infer four-channel PCM from the CD3313's four analog speaker-level
   controls. The framework primary output and Normal playback ALSA output
   remain proven stereo in the supplied artifacts.
5. HDMI, FM transmitter, Bluetooth SCO, voice, and other non-Normal handlers
   still require separate route analysis.

No HAL binary, kernel module, mixer state, GPIO, I²S configuration, or live
head-unit state was modified.
