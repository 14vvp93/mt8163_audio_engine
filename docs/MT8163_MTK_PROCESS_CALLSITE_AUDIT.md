# MT8163 MTK filter process call-site audit

## Scope and source identity

This audit follows the `AudioMTKFilterManager::process()` call from the supplied
`vendor/audio.primary.mt8163.so`.

- ELF Build ID: `f66eaf9e6ac950259f0c253be618d15a`
- HAL SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`

Static disassembly only. No runtime callback, PCM capture, or device-side
validation was performed.

## 1. Unique direct call site in the primary HAL

The dynamic symbol for
`AudioALSAPlaybackHandlerBase::doPostProcessing(void*, unsigned int, void**, unsigned int*)`
is at `0xD24B0`. The primary HAL's direct call to this wrapper is in
`AudioALSAPlaybackHandlerNormal::write()` at `0xD4744`.

A full disassembly search of this ELF found one direct call site to
`doPostProcessing()`, in the Normal handler write method. The other
exported playback write methods (HDMI, Voice, BTCVSD, FMTransmitter, BTSCO and
HfpClient) do not contain another direct call to this wrapper.

The Normal write order is:

| Call site | Stage |
|---:|---|
| `0xD4710` | `doDcRemoval()` |
| `0xD471C` | `doStereoToMonoConversionIfNeed()` |
| `0xD4744` | `doPostProcessing()` |
| `0xD4768` | `doBliSrc()` |
| `0xD478C` | `doBitConversion()` |
| `0xD47D4` | `pcmWrite()` |

Therefore this MTK filter-manager callback is not a proven global post-mix
hook. It is a processing stage on the Normal playback-handler path only.

## 2. Wrapper gate and buffer contract

`AudioALSAPlaybackHandlerBase::doPostProcessing()` at `0xD24B0`
bypasses the manager and preserves the caller's original buffer unless all
of these conditions pass:

1. The filter-manager pointer at handler `+0x1E0` is non-null.
2. `stream_attribute_t +0x120` is zero.
3. `stream_attribute_t +0x41` is zero.
4. Input byte count does not exceed scratch capacity at handler `+0x1E8`.

When eligible, the wrapper:
- calls `AudioMTKFilterManager::start(bool)` using the flag at handler
  `+0x1EC`;
- calls `AudioMTKFilterManager::process(void*, unsigned int, void*, unsigned int)`
  with the manager, input pointer, input byte count, scratch output pointer,
  and scratch capacity;
- treats the returned integer as output byte count;
- if the returned byte count is zero, keeps the original input pointer and
  original byte count;
- if nonzero, selects the scratch pointer and returned output byte count.

The scratch capacity initialized by `initPostProcessing()` is
`0x20000` bytes (128 KiB). This is a maximum buffer capacity, not the
normal per-write block size.

The wrapper's actual native process arguments are therefore byte-oriented.
The precise sample format and channel count are inherited from the Normal
handler's input stream and are not converted to Float32 by this wrapper.

## 3. Format and route interaction

At this point in the Normal write pipeline:
- source sample format is still the stream-attribute format; the helper
  recognizes format values 1 (16-bit) and 3 (32-bit);
- source sample rate and channel count are still in the source stream domain;
- BLI sample-rate/channel conversion and bit conversion happen later;
- the output-device bit `0x02` (speaker), including combined masks `0x06`
  and `0x0A`, has already triggered the stereo-to-mono fold
  `L,R -> M,M` before this callback.

For a headphone-only Normal route (`0x04` or `0x08`), the
speaker-bit fold is not selected by that helper. This does not prove that all
such streams are enabled for MTK processing: the wrapper's independent
`+0x120` and `+0x41` bypass gates still apply.

The Normal handler's target ALSA channel count is two. Do not confuse that
downstream target with the pre-BLI source channel field at
`stream_attribute_t +0x24`.

## 4. Integration consequences

1. A HAL adapter based on this callback would initially affect only streams
   that use the Normal playback handler and pass the wrapper's bypass gates.
2. The speaker route reaches the callback after its stereo content has already
   been folded to mono and duplicated.
3. The callback is not a four-channel FL/FR/RL/RR PCM boundary.
4. Do not inject a Float32 quad buffer or change the callback's byte counts
   without a fully recovered adapter contract.
5. Preserve the original input pointer/length on every unsupported format,
   bypass condition, or DSP failure.
6. Map the other handlers and their lifecycle independently before claiming
   coverage for HDMI, FM, Bluetooth SCO, voice calls, or capture.

No HAL binary, kernel module, mixer state, GPIO, I²S configuration, or live
head-unit state was modified.
