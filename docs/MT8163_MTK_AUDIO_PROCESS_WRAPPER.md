# MT8163 MtkAudioLoud PCM Process Wrapper

## Binary provenance

Analyzed the supplied `lib/libaudiocomponentengine_vendor.so`:

- ELF32 ARM EABI5, Thumb-2 code
- Build ID: `9a4bcc2b143dac2e43ef73a56373b2cb`
- SHA-256: `1f2d0a1b046f68bd522edd8b49d3e746768d2fa406e463b617bc7030aaf20ce6`

This note documents the exported `android::MtkAudioLoud::process(void*, unsigned int*, void*, unsigned int*)` wrapper at symbol address `0x5735` (Thumb entry; code at `0x5734`).

## Confirmed wrapper behavior

The method is not a simple one-shot pass-through. The disassembly shows a chunked processing loop:

1. It reads the configured work/chunk-size field from object offset `+0x9B8`.
2. It reads the input-length value through the second argument pointer.
3. It derives a maximum number of processing iterations using integer division by the configured chunk size.
4. It initializes running input/output byte counters to zero.
5. For each iteration, it checks that enough input remains for the next chunk.
6. It invokes an indirect processing callback loaded from object offset `+0x4C`.
7. The callback receives an internal work/context pointer derived from `this + 0x38`, an input pointer advanced by the running input offset, an output pointer advanced by the running output offset, and stack arguments carrying the relevant size/count pointers.
8. After each callback, the wrapper reads the updated input/output lengths and accumulates the consumed/produced byte counts.
9. It writes the final accumulated input/output counts back through the caller-provided pointers.
10. If the input cannot supply the next complete chunk, it calls a separate helper with selector/value `5` and the remaining-size information rather than invoking the regular full-chunk callback.

A separate branch is taken when the object mode field at `this + 0x04` is not `2`; it invokes another helper and returns a fixed small result path. In mode 2, the chunked callback path is used.

## What this proves

- The vendor component has a chunk-oriented processing wrapper around a pluggable/indirect DSP callback.
- The work chunk size is held in the object at `+0x9B8`.
- The callback pointer is held at `+0x4C`.
- The wrapper maintains separate consumed-input and produced-output byte counts.
- Partial trailing input is handled through a distinct helper path, not by blindly calling the full-chunk processor.

## What this does not prove

The wrapper alone does not identify:

- PCM sample format (16-bit, 24-bit, float, interleaved/planar);
- channel count or channel order;
- sample rate;
- whether the callback is BES, a DC-removal stage, a SRC stage, or another processor;
- exact callback argument semantics beyond the register/stack data flow;
- whether this processor is active on every Android output route.

Do not connect the custom DSP to this callback based only on the wrapper. The object must be traced through construction, `init()`, `open()`, `setWorkMode()`, and the filter-manager process call sites first.

## Next reverse-engineering steps

1. Trace writes to `this + 0x9B8` to identify chunk-size units and initialization conditions.
2. Trace writes to `this + 0x4C` to identify the actual callback target(s).
3. Recover the exact stack/register contract at each indirect call target.
4. Trace `MtkAudioLoud::open()` and `init()` to recover the mode and callback lifecycle.
5. Correlate this processor with `AudioMTKFilterManager::process()` and the HAL's PCM buffer format.

No HAL patch, device write, CD3313 command, or runtime audio routing change was performed.
