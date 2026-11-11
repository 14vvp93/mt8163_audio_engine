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

## 1. Start dispatch table

`AudioALSAHardwareResourceManager::startOutputDevice_l(unsigned int, unsigned int)`
at `0x74374` stores the first argument as the current internal output selector,
subtracts one, bounds-checks it against 0..9, and dispatches through a ten-entry
jump table. The selected branch loads a function pointer from the resource
manager vtable and invokes it with the device-operation context and second
argument.

The exact vtable slots read by the disassembly are:

| Internal selector | Vtable byte offset | Resolved callback |
|---:|---:|---|
| 1 | +0xB0 | `aee_system_exception` (unresolved route semantics) |
| 2 | +0xC0 | `OpenHeadphonePath(unsigned int)` |
| 3 | — | default/no callback |
| 4 | +0xB8 | `OpenReceiverPath(unsigned int)` |
| 5 | — | default/no callback |
| 6 | +0xC8 | `OpenSpeakerPath(unsigned int)` |
| 7 | — | default/no callback |
| 8 | +0xB8 | `OpenReceiverPath(unsigned int)` |
| 9 | — | default/no callback |
| 10 | +0xC8 | `OpenSpeakerPath(unsigned int)` |

These are internal one-based selector values. They must not be equated with
Android `audio_devices_t` bit masks without tracing the upstream conversion.

## 2. Stop dispatch table — corrected from direct branch-target calculation

`AudioALSAHardwareResourceManager::stopOutputDevice_l()` at `0x74400`
uses a second ten-entry table and bounds-checks the same one-based selector.
The branch table base is `0x74428`; calculating each target from that base
resolves the following vtable offsets:

| Internal selector | Vtable byte offset | Resolved callback |
|---:|---:|---|
| 1 | +0xB4 | `setAudioDebug(bool)` (does not form a known route-close pair) |
| 2 | +0xC4 | `CloseHeadphonePath()` |
| 3 | — | default/no callback |
| 4 | +0xBC | `CloseReceiverPath()` |
| 5 | — | default/no callback |
| 6 | +0xCC | `CloseSpeakerPath()` |
| 7 | — | default/no callback |
| 8 | +0xBC | `CloseReceiverPath()` |
| 9 | — | default/no callback |
| 10 | +0xCC | `CloseSpeakerPath()` |

This table supersedes the earlier slot interpretation in this document.
The corrected offsets were read directly from the four dispatch load
instructions at `0x74454`, `0x74460`, `0x7446c`, and `0x74478`,
after resolving the ten branch-table entries.

## 3. Vtable callback identity and open/close pairs

The packed ARM relocation entries in the same ELF resolve the route callback
block to these method symbols:

| Vtable byte offset | Method |
|---:|---|
| +0xB0 | `aee_system_exception` |
| +0xB4 | `AudioALSAHardwareResourceManager::setAudioDebug(bool)` |
| +0xB8 | `OpenReceiverPath(unsigned int)` |
| +0xBC | `CloseReceiverPath()` |
| +0xC0 | `OpenHeadphonePath(unsigned int)` |
| +0xC4 | `CloseHeadphonePath()` |
| +0xC8 | `OpenSpeakerPath(unsigned int)` |
| +0xCC | `CloseSpeakerPath()` |
| +0xD0 | `OpenHeadphoneSpeakerPath(unsigned int)` |
| +0xD4 | `CloseHeadphoneSpeakerPath()` |

The callback signatures and the start/stop dispatch tables establish
consistent pairs for selectors 2, 4, 6, 8 and 10:
- 2: headphone open/close;
- 4 and 8: receiver open/close;
- 6 and 10: speaker open/close.

Selector 1 is anomalous in the recovered tables: its start entry resolves to
`aee_system_exception`, while its stop entry resolves to
`setAudioDebug(bool)`. It is not assigned a physical output route here.
Do not infer that selector 1 is a valid speaker/headphone/receiver route.

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

The selector values above are internal HAL values. The upstream conversion
from Android output-device masks to these values is still unresolved.

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
configuration source, or a packaging mismatch. The live configuration has not
been inspected, so a runtime failure is not claimed.

## 6. Consequences for DSP integration

1. Do not change the XML names or patch the HAL based only on this static pair.
2. Obtain the live unit's resolved device-path configuration and mixer controls.
3. Trace the upstream conversion from Android output-device masks to the
   internal selector values; the callback mapping alone does not provide it.
4. Confirm which route actually reaches `OpenSpeakerPath()` on the live unit.
5. Keep the DSP outside this route-control layer. The audio data path remains
   the separate PCM playback-handler path; GPIO and mixer sequences are
   hardware-control operations, not PCM processing.
6. Do not infer four-channel PCM from the CD3313's four analog speaker-level
   controls. The framework primary output and Normal playback ALSA output
   remain proven stereo in the supplied artifacts.

No HAL binary, kernel module, mixer state, GPIO, I²S configuration, or live
head-unit state was modified.
