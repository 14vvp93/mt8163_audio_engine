# MT8163 ACF Structure Reconstruction

## Scope and provenance

This note records findings from ARM Thumb-2 disassembly of the supplied vendor library:

- File: `lib/libaudiocomponentengine_vendor.so`
- ELF: 32-bit ARM EABI5, Android 28
- Build ID: `9a4bcc2b143dac2e43ef73a56373b2cb`
- SHA-256: `1f2d0a1b046f68bd522edd8b49d3e746768d2fa406e463b617bc7030aaf20ce6`

Related supplied libraries were also identified:

- `lib/libbessound_hd_mtk_vendor.so` — SHA-256 `337240ff5f598490e3848d26aa3720452c0956e57605a0f372b8070c0149520c`
- `lib/libaudiocompensationfilter_vendor.so` — SHA-256 `0ccc211a70b3f8e281c39e4ac6727f60f745a6381b229c77e64ec812078a36f3`

The findings below describe this exact binary set. They do not establish that every MT8163 firmware image uses the same ABI.

## Confirmed ACF object size

The `MtkAudioLoud::setParameter(unsigned int, void*)` implementation contains a copy of exactly `0x4EC` bytes for selector 6. This agrees with the 1260-byte size previously recovered from the HAL's custom-parameter handling. The copy destination is the object's internal area at `this + 0x5C`.

A matching size is strong evidence that the vendor component and HAL exchange the same ACF-sized payload. It is not, by itself, proof that every byte is initialized or that the payload can safely be constructed outside the vendor implementation.

## Recovered copyParam() field map

`MtkAudioLoud::copyParam()` writes into the ACF structure reached through the object/filter-manager pointer chain. The following are direct source-to-destination copies observed in the disassembly. Source offsets are relative to the `MtkAudioLoud` object (`this`); destination offsets are relative to the ACF structure.

| ACF destination | Source in object | Size | Operation |
|---:|---:|---:|---|
| `0x000` | `0x140` | 4 | scalar copy |
| `0x004` | `0x144` | 4 | scalar copy |
| `0x008` | `0x148` | 4 | scalar copy |
| `0x00C` | `0x13C` | 1 / normalized flag | nonzero becomes 1 |
| `0x010` | `0x14C` | 4 | scalar copy |
| `0x014` | `0x150` | 4 | scalar copy |
| `0x018` | `0x154` | 16 | vector copy |
| `0x024` | `0x160` | 16 | vector copy |
| `0x034` | `0x170` | 160 (`0xA0`) | block copy |
| `0x0D4` | `0x210` | 160 (`0xA0`) | block copy |
| `0x174` | `0x2B0` | 16 | vector copy |
| `0x184` | `0x2C0` | 16 | vector copy |
| `0x194` | `0x2D0` | 16 | vector copy |
| `0x1A4` | `0x2E0` | 16 | vector copy |
| `0x1B4` | `0x2F0` | 4 | scalar copy |
| `0x1B8` | `0x2F4` | 192 (`0xC0`) | block copy |
| `0x278` | `0x3B4` | 192 (`0xC0`) | block copy |
| `0x338` | `0x474` | 192 (`0xC0`) | block copy |
| `0x3F8` | `0x534` | 4 | scalar copy |
| `0x3FC` | `0x538` | 4 | scalar copy |
| `0x400` | `0x53C` | 4 | scalar copy |
| `0x404` | `0x540` | 4 | scalar copy |

The table intentionally uses neutral names such as scalar, vector, and block. The disassembly does not, on its own, justify assigning all of these regions semantic labels such as EQ, DRC, limiter, or loudness parameters.

The routine also has conditional paths that inspect filter state and update/reset internal buffers. Those paths are not simple structure serialization and should be analyzed separately before treating `copyParam()` as a complete inverse of the vendor parser.

## Sampling-rate index

`MtkAudioLoud::getBesLoudSamplingRateIndex(unsigned int)` contains explicit comparisons for the following exact rates:

| Sample rate | Returned index |
|---:|---:|
| 48,000 Hz | 0 |
| 44,100 Hz | 1 |
| 32,000 Hz | 2 |
| 24,000 Hz | 3 |
| 22,050 Hz | 4 |
| 16,000 Hz | 5 |
| 12,000 Hz | 6 |
| 11,025 Hz | 7 |
| 8,000 Hz | 8 |
| any other rate | -1 |

This is the vendor BES lookup table, not a statement that every Android output route supports all nine rates.

## V4-to-V5 coefficient conversion

The exported method is:

`MtkAudioLoud::convertBesLoudCoeffV4ToV5(unsigned int, _AUDIO_ACF_CUSTOM_PARAM_STRUCT_FILTER_PARAM*, BLOUD_HD_FilterCoef*)`.

The disassembly confirms:

1. It resolves/validates a BES sampling-rate index and rejects indices greater than 8.
2. It calculates a rate-dependent source offset using 20 bytes per index.
3. It iterates five records. Each source record advances by `0xB4` bytes; each output record advances by `0x14` bytes.
4. For each of those five records it copies a 16-byte coefficient block and a 32-bit value at source offset `+0x10`.
5. A further conversion branch is present for rate indices up to 5. Its full field semantics are not yet named.

This establishes record strides and copy widths, but not the mathematical coefficient representation (for example Q format, floating point, or normalized fixed point). That requires tracing the consuming BES API and its coefficient arithmetic.

## setParameter() selector observations

The method uses a 16-entry Thumb branch table for selectors 0 through 15. Directly visible effects include:

- Selector 0 stores the supplied pointer in an object field at `this + 0x0C`.
- Selector 2 accepts scalar values 1 or 2 and stores the accepted value at `this + 0x24`.
- Selector 3 stores the supplied scalar at `this + 0x28`.
- Selector 4 stores the supplied scalar at `this + 0x2C) and `this + 0x08`.
- Selector 6 copies `0x4EC` bytes from the supplied pointer to `this + 0x5C`.
- Selector 14 sets an internal field at `this + 0x9B4` to either `0x50` or zero depending on the supplied scalar.
- Other selectors call internal helpers, dispatch callbacks, or alter mode state. Their exact public meaning is not inferred here.

Do not send guessed payloads to these selectors. In particular, selector numbers are internal vendor API dispatch values, not CD3313 kernel ioctl command numbers.


## BES fixed-point formats

The companion `libbessound_hd_mtk_vendor.so` exports coefficient conversion helpers whose names and machine code expose additional numeric formats:

- `Format_F64_to_Q5p27`
- `Format_F64_to_Q2p14`
- `Copy_Q1p31_To_Q1p15`
- `Copy_Q1p15_To_Q1p31`
- `Filter_Coef_32Bit`
- `Filter_Coef_16Bit`

The two `Format_F64` routines use VFP double-precision arithmetic and convert the results to 32-bit integer words before packing them into the destination coefficient structure. Their exported names identify Q5.27 and Q2.14 output formats respectively. Exact clipping/rounding and sign treatment still need to be documented from the full numeric path; the names alone are not enough to define a compatible external encoder.

The Q-format width conversion helpers are more explicit:

- `Copy_Q1p15_To_Q1p31` widens each 16-bit value by shifting it left 16 bits. The NEON path uses `vshll.i16 #16`; the scalar tail uses `lsl #16`.
- `Copy_Q1p31_To_Q1p15` narrows each 32-bit value by taking the upper 16 bits. The NEON path uses `vshrn.i32 #16`; the scalar tail reads the halfword at byte offset `+2`. No rounding step is visible in either path.

These routines establish representation conversions used by BES. They do not prove that the ACF payload itself stores every filter coefficient in one of these formats; the V4/V5 converter and each consumer must still be traced.

## Disassembly correction

The selector-4 observation above refers to the object fields `this + 0x2C` and `this + 0x08` (without a trailing parenthesis). The selector table remains a dispatch map; selector names/meanings have not been inferred.

## Safety and next analysis

No CD3313 ioctl or register write is performed by this work. These findings concern the MediaTek software ACF/BES processing stack.

Next steps:

1. Trace the ACF structure's producer/parser to identify the semantic type of each copied region.
2. Decode the complete selector branch table and helper-call argument contracts.
3. Trace how the BES library consumes `BLOUD_HD_FilterCoef` and determine coefficient numeric format.
4. Compare the vendor process path with the HAL's `AudioMTKFilterManager::process()` call path.
5. Keep the custom PCM engine offline until the buffer format, stream lifecycle, and safe media-only insertion point are proven.
