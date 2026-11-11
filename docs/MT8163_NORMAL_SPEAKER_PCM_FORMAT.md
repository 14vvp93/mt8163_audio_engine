# MT8163 Normal speaker PCM format audit

## Scope and source identity

This audit follows the standard Normal playback handler in the supplied
`vendor/audio.primary.mt8163.so`.

- ELF Build ID: `f66eaf9e6ac950259f0c253be618d15a`
- HAL SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`

Static disassembly only; no live PCM capture or hardware test was performed.

## 1. Exact write-stage order

`AudioALSAPlaybackHandlerNormal::write()` at `0xD465C` calls these stages
in this order:

| Call site | Stage |
|---:|---|
| `0xD4710` | `doDcRemoval()` |
| `0xD471C` | `doStereoToMonoConversionIfNeed()` |
| `0xD4744` | `doPostProcessing()` |
| `0xD4768` | `doBliSrc()` |
| `0xD478C` | `doBitConversion()` |
| `0xD47D4` | `pcmWrite()` |

Therefore, the MTK post-processing boundary is downstream of the conditional
stereo-to-mono conversion, but upstream of sample-rate conversion and bit
conversion.

## 2. Speaker-bit condition and exact operation

`AudioALSAPlaybackHandlerBase::doStereoToMonoConversionIfNeed()` is at
`0xD20AC`. It:
1. loads the `stream_attribute_t*` stored at handler offset `+0x0C`;
2. reads the low byte at `stream_attribute_t +0x18`;
3. tests bit `0x02`;
4. bypasses conversion if that bit is clear.

The output-device field at `+0x18` is also passed unchanged to
`AudioALSAHardwareResourceManager::startOutputDevice()` by the Normal
handler's `open()` path. The output-route audit resolves `0x02` as
`AUDIO_DEVICE_OUT_SPEAKER`.

When bit `0x02` is set:
- PCM format value 1: the helper reads adjacent signed 16-bit samples,
  shifts each right by one, adds them, then writes the same 16-bit result
  into both positions.
- PCM format value 3: it reads adjacent 32-bit samples, arithmetic-shifts
  each right by one, adds them, then writes the same 32-bit result into both
  positions.
- Other format values bypass this conversion.
- The input byte count is not changed.

The operation is an in-place stereo-to-mono fold followed by duplication:
`L,R -> M,M`. It is not a four-channel expansion.

## 3. Consequence for the MTK filter boundary

For Normal-handler streams whose output-device mask contains
`AUDIO_DEVICE_OUT_SPEAKER (0x02)`, including the combined masks `0x06`
and `0x0A`, the MTK post-processing stage receives a two-channel buffer
whose left and right sample values have already been made identical by this
helper.

Consequently:
- a DSP attached only at `AudioMTKFilterManager::process()` on this path
  cannot recover the original left/right stereo difference;
- that boundary is not a four-independent-channel PCM interface;
- the Normal handler's downstream ALSA output remains configured for two
  channels.

This does not prove that the external CD3313 or amplifier stage cannot derive
or distribute multiple analog outputs from the two-channel/mono feed. That
requires live mixer/PCM inspection and board or Device Tree topology evidence.

## 4. Integration constraints

1. Do not feed a four-channel buffer into the Normal handler's existing
   two-channel BLI/ALSA path.
2. Do not assume a four-channel DSP profile corresponds to four discrete
   PCM streams on this hardware.
3. If independent FL/FR/RL/RR processing is required, first locate the
   actual split point: an upstream multichannel PCM route, a downstream
   hardware DSP/codec path, or an explicit channel-mapping stage.
4. Preserve the stock mono-fold behavior unless the physical route and
   intended speaker behavior are established on the live head unit.
5. Validate with PCM captures and mixer/device diagnostics before any
   runtime integration.

No HAL binary, kernel module, mixer state, GPIO, I²S configuration, or live
head-unit state was modified.
