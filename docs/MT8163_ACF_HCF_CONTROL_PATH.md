# MT8163 HAL ACF/HCF control-path findings

**Source:** GitHub Actions artifact `mt8163-audio-hal-analysis`, generated from the supplied ARM32 `audio.primary.mt8163.so` (Build ID `f66eaf9e6ac950259f0c253be618d15a`). This report records static disassembly findings only; no head-unit runtime behavior is claimed.

## Confirmed from the HAL disassembly

### ACF/HCF profile update path

The implementation of `android::AudioALSAStreamManager::UpdateACFHCF(int)` at file offset/virtual address `0x7f18c`:

1. Checks the stream manager field at offset `0xb0`; if zero, it skips the update loop.
2. Iterates over the filter-manager pointer array rooted at object offset `0xac`, using entries with an 8-byte stride.
3. For the update selector passed as the function argument:
   - selector `0`: obtains a custom parameter structure using `getAudioCompFltCustParam(AudioCompFltType_t(0), ...)`, then calls `AudioMTKFilterManager::setParameter(0, struct)`;
   - selector `1`: obtains type `1`, then calls `setParameter(1, struct)`;
   - selector `2`: obtains type `4`, then calls `setParameter(4, struct)`;
   - other values skip the per-filter update.
4. The call is repeated for each configured filter-manager entry.

This confirms three distinct ACF/HCF parameter classes are loaded and dispatched through the MTK filter manager. It does **not** establish the full structure layout, coefficient format, or the semantics of every downstream filter slot.

### Preview API forwarding

`AudioALSAHardware::SetACFPreviewParameter(void*, int)` at `0xce4bc` forwards the supplied pointer and integer to `AudioALSAStreamManager::SetACFPreviewParameter(void*, int)` through the stream-manager pointer stored at object offset `+4`. The HCF counterpart at `0xce510` similarly forwards to `SetHCFPreviewParameter`.

The HAL exports/contains the related ACF/HCF preview entry points and update strings, including `SetACFPreviewParameter`, `SetHCFPreviewParameter`, `UpdateACFHCF`, and `UpdateACFHCFParameters`. This is a real vendor-HAL control surface worth tracing from the framework/service side before considering any custom integration.

### Relevant dependency evidence

The HAL imports the custom-parameter functions `getAudioCompFltCustParam` and `setAudioCompFltCustParam`. Its strings reference both:

- `/vendor/lib/libaudiocompensationfilter_vendor.so`
- `/system/lib/libaudiocompensationfilter.so`

The HAL also references `libaudio_param_parser-vnd.so`. These are evidence of the ACF parameter-loading stack, not proof that any one library talks directly to CD3313.

## Integration implications

- The stock HAL already has a vendor-supported ACF/HCF update and preview path. Prefer understanding and reusing that path over replacing the HAL or inventing a new ioctl.
- The likely next static-analysis targets are the matching ACF/HCF implementations and custom-parameter parser libraries, especially the functions that consume the parameter structures and eventually reach the audio processing backend.
- Trace callers of `UpdateACFHCF`, `SetACFPreviewParameter`, and `SetHCFPreviewParameter` in framework/service binaries to recover the public control path and lifecycle.
- Recover the exact parameter structure definitions from matching headers, debug symbols, or parser code before constructing any payload.
- Keep this ACF/HCF path separate from CD3313 hardware control. The evidence here does not identify a CD3313 device node, ioctl, I2C transport, or chip register mapping.

## Confidence

| Finding | Confidence |
|---|---|
| UpdateACFHCF loads parameter types 0, 1, and 4 and dispatches selectors 0, 1, and 4 | High — direct ARM32 disassembly |
| Preview methods forward pointer + integer to stream manager | High — direct ARM32 disassembly |
| ACF/HCF dependencies are part of the vendor processing stack | High — dynamic imports and dependency strings |
| These paths control CD3313 directly | Unproven — no direct transport evidence in this analysis |

## Safety boundary

Do not call these preview/update methods with guessed pointers, lengths, selector meanings, or structures. Do not replace the primary HAL. Runtime probing requires the matching Android framework/service interface and a reversible test plan on the target head unit.
