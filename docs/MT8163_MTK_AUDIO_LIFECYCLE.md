# MT8163 MtkAudioLoud Initialization and Work-Mode Lifecycle

## Binary provenance

This analysis targets the supplied `lib/libaudiocomponentengine_vendor.so`:

- ELF32 ARM EABI5, Android 28
- Build ID: `9a4bcc2b143dac2e43ef73a56373b2cb`
- SHA-256: `1f2d0a1b046f68bd522edd8b49d3e746768d2fa406e463b617bc7030aaf20ce6`

Thumb-2 disassembly was performed with the ARMv7-A/Thumb target. Addresses below are code addresses with the Thumb bit cleared.

## init() allocations and object regions

`MtkAudioLoud::init()` at `0x480c` performs these directly visible initializations:

- Clears/initializes the 16-byte region at object offset `+0x24`.
- Clears the 16-byte region at `+0x38).
- Clears the 16-byte region at `+0x44).
- Initializes the ACF-sized region at `+0x5C` with length `0x4EC` (1260 bytes).
- Initializes a region at `+0x5C0` with length `0x3F0` (1008 bytes).
- Initializes a region at `+0x550` with length `0x70` (112 bytes).
- Allocates a 0x18-byte context/descriptor and stores its pointer at `+0x34`.
- Allocates two `0x108`-byte regions and stores their pointers in the descriptor at offsets `+0x10` and `+0x00).
- Allocates a `0x40C`-byte region and stores its pointer in the descriptor at offset `+0x0C).
- Sets the object state field at `+0x04` to 1.
- Clears state bytes around `+0x9C9) and `+0x9CD`.

The allocation sizes are observed constants. Their full semantic types and the internal meaning of every descriptor pointer remain to be recovered.

## Constructor defaults

Both visible constructors initialize the object work-size field at `+0x9B8` to `0x200` (512). This is a default internal unit count, not yet proven to be a byte count or PCM frame count.

The constructor also clears state at `+0x9B0` and `+0x9B4`, and initializes the object vtable. The one-argument constructor dispatches an initial virtual `setParameter` call with selector 0 and its constructor argument.

## open() lifecycle

`MtkAudioLoud::open()` at `0x51c0` checks the object state field at `+0x04`:

- If the state is not 1, it takes the non-open path.
- In state 1, it initializes the work context at `+0x38` through a helper.
- It calls the function pointer stored at `+0x38), passing pointers to object fields at `+0x14` and `+0x18), plus the value at `+0x08).
- It conditionally resizes/reallocates buffers referenced through fields at `+0x14), `+0x18), `+0x54), and `+0x58), using object size/capacity fields at `+0x1C` and `+0x20`.
- It calls a second function pointer at `+0x3C), passing the work context, a buffer pointer, and the object region at `+0x24).
- On the successful path, it changes the object state at `+0x04` to 2.

The two callback slots at `+0x38` and `+0x3C` are therefore part of the open/setup lifecycle. They are distinct from the process callback loaded from `+0x4C`.

## setWorkMode() contract

`MtkAudioLoud::setWorkMode(unsigned int, unsigned int, unsigned int, bool)` at `0x537c` refuses to reconfigure when the object is already in state 2. Otherwise it dispatches four virtual parameter calls in sequence:

1. selector 2 with the first unsigned argument;
2. selector 3 with the second unsigned argument;
3. selector 1 with the third unsigned argument;
4. selector 12 with the boolean argument.

The routine stops and returns on the first nonzero result. This is direct evidence of the selector sequence used by this wrapper, but the public semantic names of the four parameters still require tracing the callers.

## process() mode and block calculation

`MtkAudioLoud::process(void*, unsigned int*, void*, unsigned int*)` at `0x5734`:

- Requires object state `+0x04 == 2` for the chunked processing path.
- Reads the work-size field at `+0x9B8`.
- Selects a left shift of 1 or 2 bits based on object fields at `+0x24` and `+0x08`.
- Computes the per-iteration amount as `work_size << shift`.
- Compares that amount with the caller-supplied input length, then computes the maximum full-chunk iteration count using unsigned division.
- Invokes the process callback loaded from `+0x4C` for each complete chunk.
- Uses `+0x54) as an additional callback argument/context value.
- Maintains separate input-consumed and output-produced counters.
- Sends a trailing incomplete input remainder through a distinct helper path with selector/value 5.
- Writes the final consumed/produced counts through the caller's length pointers.

Therefore, `+0x9B8` is not safely described as a byte count by itself. The effective chunk amount is derived from this field and the selected shift. The precise unit (frames, samples, or bytes) depends on the mode fields and callback contract, which are not yet fully named.

## Callback assignment — resolved by cross-binary trace

The initial direct-disassembly scan did not reveal a simple store to object offset +0x4C. Subsequent cross-binary analysis resolved this through BLOUD_HD_SetHandle(): MtkAudioLoud::open() passes this +0x38 to that initializer, which writes six consecutive callback pointers. The sixth pointer is 0xC088 and occupies MtkAudioLoud +0x4C. MtkAudioLoud::process() loads and invokes that field for each complete chunk.

The callback's six-argument ABI and its call into BLOUD_HD_Internal_Process() are documented in the later sections below. The unresolved work is now the detailed PCM sample/channel format and semantic decoding of BES state fields, not callback identity.

## BES library callback-table initialization

A second supplied binary was inspected alongside the component engine:

- `lib/libbessound_hd_mtk_vendor.so`
- ELF32 ARM, Android 25
- Build ID: `fd0c2beebee1feb48671e3c43e7f6ae9`
- SHA-256: `337240ff5f598490e3848d26aa3720452c0956e57605a0f372b8070c0149520c`

### BLOUD_HD_SetHandle()

The exported function at `0xBDA4` initializes a six-entry function table through its first argument. It returns `-1` for a null first argument and `0` after populating the table.

The six pointer slots are written at offsets:

| Offset | Resolved target |
|---:|---:|
| `+0x00` | `0xBE40` |
| `+0x04` | `0xBFA8` |
| `+0x08` | `0xBE70` |
| `+0x0C` | `0xBF7C` |
| `+0x10` | `0xBE20` |
| `+0x14` | `0xC088` |

These are internal function entry points in the same BES library. This is a callback/function table initializer, not evidence that the table is the same object as the `MtkAudioLoud` fields at `+0x38), `+0x3C), or `+0x4C`. That relationship still needs a caller-side data-flow trace.

### BLOUD_HD_Filter_Design()

The exported function at `0xC1B0` performs visible argument checks before dispatching to an internal routine:

- `r2` and `r3` must both be non-null.
- The filter index in `r0` must be no greater than 5.
- The sample-rate value in `r1` is checked against a lower bound of 8,000 and an upper bound of 191,488 (derived from the immediate comparison in the disassembly).
- On valid arguments it dispatches through an internal PLT call and returns zero.

The observed error paths distinguish null pointers (`-1`), an out-of-range rate (`-2`), and a valid-rate call with an invalid filter index (`-3`). These are machine-level return paths; their public API meanings are not documented here.

### What this establishes — and what it does not

The BES binary contains a concrete function-table initializer and a filter-design entry point with explicit validation. This materially narrows the reverse-engineering task: the next step is to trace where the component engine allocates the table, calls `BLOUD_HD_SetHandle`, and stores or forwards the returned function pointers.

It does **not** yet establish:
- complete public C prototypes or semantic names for all six callbacks;
- the exact sample format, channel layout, interleaving, or PCM units used by the streaming path;
- the full semantic meaning of the internal BES state fields and parameter records.

No DSP implementation, stock HAL, CD3313 command, or runtime audio route was modified during this analysis.


## Cross-binary callback ABI match

The six-entry table created by `BLOUD_HD_SetHandle()` strongly matches the six consecutive function-pointer fields beginning at `MtkAudioLoud +0x38`. This is supported by both the exact slot layout and the argument shapes at the component-engine call sites:

| MtkAudioLoud field | BES table slot | BES target | Observed use in component engine |
|---:|---:|---:|---|
| `+0x38` | `+0x00` | `0xBE40` | `open()` calls it with three register arguments |
| `+0x3C` | `+0x04` | `0xBFA8` | `open()` calls it with three register arguments |
| `+0x40` | `+0x08` | `0xBE70` | Used as a callback from parameter/work-mode paths |
| `+0x44` | `+0x0C` | `0xBF7C` | BES helper/query entry; caller use still being traced |
| `+0x48` | `+0x10` | `0xBE20` | BES helper entry; caller use still being traced |
| `+0x4C` | `+0x14` | `0xC088` | `process()` calls it for every complete PCM chunk |

The strongest match is the last slot. The component-engine `process()` call through `+0x4C` passes six arguments under the ARM procedure-call convention: a handle/table address, a context/buffer pointer, an input pointer, a pointer to the chunk input length, an output pointer, and a pointer to the produced output length. The BES target at `0xC088` also consumes six arguments, clears and updates the two length pointers, and dispatches processing through the handle's state. This is consistent with the streaming process interface; the exact semantic type names are still unknown.

The first two slots also match the `open()` call sites: `+0x38` is invoked with three register arguments and `+0x3C` is invoked with three register arguments, matching the corresponding BES entry points at `0xBE40` and `0xBFA8`.

This makes the correspondence between the BES callback table and `MtkAudioLoud +0x38..+0x4C` a strong binary-level identification, rather than a guess based only on adjacent fields. The initialization edge is confirmed: `MtkAudioLoud::open()` computes `this + 0x38` and calls the `BLOUD_HD_SetHandle()` PLT entry at `0x51EE`. A separate call in `getBesSoundVer()` passes a stack-allocated six-pointer table to the same API. Thus the callback-table-to-object-field mapping is directly established.

### Integration implication

The process callback is now identified as the BES table's sixth entry (`0xC088`) with high confidence. The remaining work is to recover the handle/context setup path, decode the data layout behind `this + 0x50` and `this + 0x54`, and trace the upstream `AudioMTKFilterManager::process()` caller to establish the real PCM format and routing conditions. This does not authorize replacing the stock HAL or bypassing its call/BT/microphone paths.


## Direct BLOUD API call-site confirmation

The component-engine call sites were matched to the imported BLOUD symbols through the ARM interworking veneers and PLT relocations:

- In `MtkAudioLoud::open()` at `0x51EE`, the code computes `this + 0x38`, places that address in `r0`, and calls the veneer resolving to `BLOUD_HD_SetHandle`. This directly confirms that the six BES function pointers are installed into the object fields `+0x38` through `+0x4C`.
- In `MtkAudioLoud::getBesSoundVer()` at `0x5BDE`, the same API is called with a stack-allocated table at `sp + 8`; the routine then invokes a function pointer loaded from that temporary table. This is a second independent use of the six-entry callback interface.
- In `MtkAudioLoud::copyParam()`, calls at `0x4FD6` and `0x5054` resolve to `BLOUD_HD_Filter_Design`. Both paths pass four register arguments and check the returned status before continuing. The two call sites use separate descriptor fields from the parameter context, so the library is asked to design more than one filter record/path during parameter preparation.

The previously provisional mapping is therefore confirmed at the initialization boundary: `BLOUD_HD_SetHandle(this + 0x38)` installs the exact six callbacks later used by `open()`, parameter/work-mode code, and `process()`. In particular, the process callback at `this + 0x4C` is the sixth BES entry at `0xC088`.

The callback argument types and the semantic meaning of each filter-design descriptor are still not fully recovered. The next analysis target is the state/context object referenced at `this + 0x50`, the buffer/context pointer at `this + 0x54), and the relationship between those fields and the two filter-design descriptor records.


## BES work-buffer and state-context setup

The first two callbacks also expose the buffer lifecycle in `MtkAudioLoud::open()`:

1. After `BLOUD_HD_SetHandle(this + 0x38)`, `open()` invokes callback slot `+0x38` (BES target `0xBE40`) with pointers to the size fields `this + 0x18` and `this + 0x14`, plus the mode value at `this + 0x08`.
2. The `0xBE40` routine accepts mode values 0 or 1. It writes `0x1F54` to the `+0x18` size field and writes either `0xF000` (61,440) or `0x11000` (69,632) to the `+0x14` size field, depending on mode. Other values take its error return path.
3. `open()` allocates/resizes the buffers at `this + 0x58` and `this + 0x54` to those respective sizes, tracking capacities at `+0x20` and `+0x1C`.
4. It then invokes callback slot `+0x3C` (BES target `0xBFA8`) with the handle-table address `this + 0x38`, the allocated `+0x58` buffer, and the parameter/context region at `this + 0x24`.
5. The `0xBFA8` routine stores its second argument into handle offset `+0x18). Since the handle-table base is `this + 0x38`, this writes the state/context pointer to `MtkAudioLoud + 0x50`.

This establishes the role of `this + 0x50` as the BES handle's state/context pointer and identifies `this + 0x58` as the allocation supplied for that state. The other allocation, `this + 0x54`, is passed as the second argument to the streaming callback at `+0x4C); its exact internal workspace semantics remain to be recovered.

The callback relationship is now directly traced end-to-end through handle creation, work-buffer sizing/allocation, state initialization, and the per-chunk process call. The remaining uncertainty is primarily the internal meaning of the BES state fields and the PCM sample/channel format, not the identity of the callback table.


## Streaming callback ABI and processing responsibilities — refined

The callback at MtkAudioLoud +0x4C is the sixth function installed by BLOUD_HD_SetHandle() (libbessound_hd_mtk_vendor.so address 0xC088). The component-engine call site at 0x57D0–0x57E6 makes its six-argument ARM AAPCS layout recoverable:

| Argument | Location at callback entry | Observed meaning |
|---:|---|---|
| 1 | r0 | BES handle/function-table base (MtkAudioLoud +0x38) |
| 2 | r1 | Auxiliary work buffer (MtkAudioLoud +0x54) |
| 3 | r2 | Current input chunk pointer |
| 4 | r3 | Pointer to the input-consumed byte/count value for this callback |
| 5 | [sp] | Current output chunk pointer |
| 6 | [sp + 4] | Pointer to the output-produced byte/count value |

A provisional ABI spelling, with neutral types, is:

    int process_cb(void *handle, void *work_buffer, const void *input,
                   uint32_t *input_consumed, void *output,
                   uint32_t *output_produced);

The parameter order is grounded in the component-engine caller and the callback's own prologue. The exact units represented by the two counters are still not fully proven; the wrapper treats them as byte/count quantities and accumulates them separately. The callback initializes both pointed-to values to zero before its work and writes updated values on return. The caller relies on these counters; the callback's integer return value is not tested at the immediate call site.

### What the 0xC088 callback actually does

The callback resolves the BES state pointer through handle offset +0x18 (which is MtkAudioLoud +0x50). It validates the handle and the two counter pointers, reads the available input/output counts, and clears both counters before processing. It derives mode-dependent chunk sizes using right/left shifts and compares the available amount against the required amount held in the BES state.

The callback is a real wrapper around the internal BES processor. Rechecking the PLT relocation order gives these call targets:

| Callback call site | PLT target | Resolved symbol | Observed role |
|---:|---:|---|---|
| 0xC118 | 0x2B98 | drvb_f0 | BES support/helper call; exact semantics not inferred |
| 0xC150 | 0x2BB0 | BLOUD_HD_Internal_Process | Internal BES processing path |
| 0xC168 | 0x2BBC | Copy_Q1p15_To_Q1p31 | 16-bit to 32-bit fixed-point buffer conversion |
| 0xC180 | 0x2BB0 | BLOUD_HD_Internal_Process | Second internal BES processing path |
| 0xC190 | 0x2BC8 | Copy_Q1p31_To_Q1p15 | 32-bit to 16-bit fixed-point buffer conversion |

The first internal-process call at 0xC150 receives the BES state pointer, the callback workspace, the current input pointer, and the output pointer. In the alternate path, the callback prepares regions at offsets +0x1000 and +0x2000 from an intermediate buffer, calls the internal processor with those intermediate regions, then converts the result back to Q1.15 for the caller. The two paths are mode/state-dependent; the exact channel arrangement and sample format are not yet established.

The callback writes separate consumed-input and produced-output counts. MtkAudioLoud::process() accumulates these values across complete chunks. Its immediate call site does not branch on the callback's integer return value, so the counters are the observable progress contract for this wrapper.

### Important distinction: BLOUD_HD_Internal_Process

BLOUD_HD_Internal_Process is exported by libbessound_hd_mtk_vendor.so at 0x6ECC (size 4048 bytes). The component-engine library does not import this symbol directly; instead, the BES callback at 0xC088 calls it through the BES library's own PLT entry at 0x2BB0. The callback contains two call sites to this function, at 0xC150 and 0xC180.

Therefore, the correct call chain is:

MtkAudioLoud::process() -> callback at MtkAudioLoud +0x4C (BES target 0xC088) -> BLOUD_HD_Internal_Process() (BES target 0x6ECC).

This is now directly established by the callback-table target and the BES PLT relocation. It is no longer merely a possible relationship. The two internal-process call sites use different buffer arrangements, so both branches must be understood before reproducing or wrapping the vendor behavior.

### Next reverse-engineering targets

1. Decode the state fields used by 0xC088, especially +0x04, +0x18, +0x1128, +0x112C, and +0x1750.
2. Trace the producer of the state object and the parameter records consumed by the callback.
3. Determine whether the gain/ramp helper branches operate on mono, stereo, or converted/interleaved buffers for each mode.
4. Trace the upstream AudioMTKFilterManager::process() caller and its buffer format before designing the Android integration adapter.

No stock HAL, DSP code, CD3313 command, or runtime audio routing was modified.


## Q1.15 / Q1.31 processing branches and intermediate buffers

Further analysis of callback 0xC088 and its PLT calls establishes two format-dependent routes into BLOUD_HD_Internal_Process():

- The callback reads a mode/format selector from BES state offset +0x1128. The state initializer at 0xBFA8 copies this field from offset +0x08 of the MtkAudioLoud parameter block, which is object offset +0x2C (the third 32-bit word in that block). The public semantic name is unknown.
- When the selector is nonzero, the callback calls BLOUD_HD_Internal_Process(state, callback_workspace, input, output) directly. The produced output count is computed as the internal sample/count amount multiplied by 4.
- When the selector is zero, it first calls Copy_Q1p15_To_Q1p31(input, callback_workspace, count). It then calls BLOUD_HD_Internal_Process(state, callback_workspace + 0x2000, callback_workspace, callback_workspace + 0x1000), and finally calls Copy_Q1p31_To_Q1p15(callback_workspace + 0x1000, output, count). The produced output count is computed as count multiplied by 2.

The conversion helpers provide strong fixed-point evidence:

- Copy_Q1p15_To_Q1p31 at 0x9198 reads 16-bit values and shifts them left by 16 bits into 32-bit words. The NEON path uses VSHLL with shift 16; the scalar tail uses LDRH followed by LSL #16.
- Copy_Q1p31_To_Q1p15 at 0x9210 extracts the upper 16 bits of each 32-bit word. The NEON path uses VSHRN with shift 16; the scalar tail reads the halfword at byte offset +2. No rounding operation is visible in these conversion paths.

Thus the zero-selector route is a Q1.15 input/output path with a Q1.31 intermediate processing representation. The nonzero-selector route uses a four-byte-per-count input/output representation directly. The selector is copied from the MtkAudioLoud initialization parameter block into state +0x1128; its user-facing meaning and the full set of legal values are not yet recovered.

The intermediate workspace is partitioned at least at offsets 0x0000, 0x1000, and 0x2000 in the Q1.15 route. The callback's workspace allocation is 0xF000 or 0x11000 bytes, so these regions lie within the allocated block. Exact per-region capacity, alignment requirements, and any additional state-dependent use remain to be decoded.

These findings do not yet prove whether the data is mono/stereo, interleaved/planar, or exactly equivalent to Android PCM16/PCM32 at the HAL boundary. Q1.15 and Q1.31 describe the internal fixed-point representation; they are not sufficient by themselves to identify the upstream PCM channel layout.
