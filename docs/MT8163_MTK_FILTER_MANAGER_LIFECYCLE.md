# MT8163 MTK filter-manager lifecycle and route selection

## Scope and source identity

Static analysis of the supplied `vendor/audio.primary.mt8163.so`:

- ELF Build ID: `f66eaf9e6ac950259f0c253be618d15a`
- SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`

This document describes code paths in this binary, not confirmed runtime
feature flags or the live head-unit configuration.

## 1. Manager creation and registration

The `AudioALSAStreamManager::openOutputStream()` path at `0x77E30`
allocates an `AudioMTKFilterManager`, invokes its four-argument constructor
at `0x9D8A0`, calls `init(0)` at `0x9DB08`, and inserts the manager into
a `SortedVector` owned by the stream manager.

The vector is stored around stream-manager offsets `+0xA8` through `+0xB0`.
The manager also stores a lookup key at `+0x4C`. The exact semantic name of
that key is not recovered. Later, `createPlaybackHandler()` looks up a value
from `stream_attribute_t +0x48` in this vector; this is the corresponding
lookup path, but the key's domain should not be named until the stream
attribute field is fully reconstructed.

The four constructor arguments are stored at manager offsets `+0x00`,
`+0x04`, `+0x08`, and `+0x0C`. The fourth argument is later used as the
size passed to `new[]` for the manager scratch buffer at `+0x28`. The
semantic names and units of the first three arguments remain unresolved.

## 2. Attaching the manager to the playback handler

In `AudioALSAStreamManager::createPlaybackHandler()` at `0x79368`:

1. The manager vector is searched using the value at
   `stream_attribute_t +0x48`.
2. On a successful lookup, the associated `AudioMTKFilterManager*` is
   passed to the selected playback handler through virtual slot `+0x38`.
3. The same filter manager receives `setDevice(stream_attribute_t +0x18)`.

The Normal handler overrides `setFilterMng(AudioMTKFilterManager*)` at
`0xD4910`. That method stores the pointer at handler offset `+0x1E0`,
which is the pointer later tested by `doPostProcessing()`.

The base-class `setFilterMng()` implementation is only a default stub that
returns `-ENOSYS`; the Normal override is the implementation relevant to
this path.

`AudioMTKFilterManager::setDevice(unsigned int)` at `0x9E314` stores the
output-device mask unchanged at manager offset `+0x2C`.

## 3. Objects initialized by AudioMTKFilterManager::init()

The manager constructor zero-initializes the object-pointer region beginning
at `+0x14). The inspected `init(0)` path:

- always allocates and initializes an `AudioMTKFilter` object at `+0x14`;
- checks `IsAudioSupportFeature(3)`;
- when feature 3 is supported, allocates and initializes an
  `AudioMTKFilter_VibSpkAddTone` object at `+0x24`, a second
  `AudioMTKFilter` object at `+0x20`, and a scratch array at `+0x28`;
- allocates the `+0x28` scratch array using constructor argument 4 as its
  byte count.

The `AudioMTKFilter::init()` method creates its inner processing object
through `newMtkAudioLoud(type)`, passing the filter object's type field at
`+0x04). The manager's always-created slot `+0x14` is configured as
`AudioMTKFilter` type 0, mode 4; the feature-3-dependent slot `+0x20` is
type 3, mode 4. These numeric type/mode values are observed constructor
arguments; their enum names are not inferred. Each wrapper stores the
returned `MtkAudioLoud*` at its own offset `+0x1C`.

The manager also has pointer slots at `+0x18` and `+0x1C`.
The constructor zeroes the pointer region; the complete manager-method
disassembly contains no non-null assignment to either slot (the only stores
to those manager offsets are nulling stores in `deinit()`). The process and
parameter methods test them for null. In the default construction path these
two slots therefore remain null, so the headphone branch's optional filter
calls are skipped and the manager returns zero for that branch. This conclusion
is limited to the supplied binary and its recovered manager lifecycle.

The numeric `AudioCompFltType_t` / `AudioComFltMode_t` values used to
configure the filter objects are preserved as raw values in this audit;
their semantic names are not inferred.

## 4. Route-conditioned processing inside AudioMTKFilterManager::process()

`AudioMTKFilterManager::process()` at `0x9E31C` reads the device mask at
manager `+0x2C` and selects a processing branch:

| Device-mask test | Branch behavior |
|---|---|
| `mask & 0x02` | Speaker branch: tests/processes filter slot `+0x14`; if feature 3 is supported and slot `+0x20` is present/active, processes that filter too, then invokes `AudioMTKFilter_VibSpkAddTone::DoVibSignal2DLProcess()` through slot `+0x24`. |
| otherwise, `mask & 0x0C` | Headphone branch: tests optional filter slots `+0x1C` and `+0x18`; each is processed only when present and active. |
| otherwise, `mask & 0x01` | Earpiece branch: requires feature 3 and feature 2, then tests/processes slot `+0x20` and may invoke the VibSpkAddTone object at `+0x24`. |
| none of the above | Returns zero (bypass). |

The process method checks each selected filter's active state under that
filter object's mutex before invoking its processing function. The manager's
filter process calls are byte-count based; the wrapper treats the final
returned value as the output byte count.

The speaker-specific setters corroborate the role of slot `+0x14`:
`setSpkOutputGain()` at `0x9E7F8` and `setSpkFilterParam()` at
`0x9E864` both operate on the filter object at manager `+0x14`. The
manager's parameter selector 0 also targets this slot. Slot `+0x20` is
the feature-3-dependent second filter used by the speaker/earpiece branches.

The branch order matters for combined masks: any mask containing speaker bit
`0x02` selects the speaker branch before the headphone branch is considered.
This is consistent with the Normal handler's earlier stereo-to-mono fold for
speaker-containing routes.

## 5. Parameter dispatch

`AudioMTKFilterManager::setParameter(unsigned int, void*)` at `0x9E754`
has a five-entry selector table. Direct branch-target calculation gives:

| Selector | Manager slot | Downstream method |
|---:|---:|---|
| 0 | `+0x14` | `AudioMTKFilter::setParameter2Sub()` |
| 1 | `+0x18` | `AudioMTKFilter::setParameter2Sub()` |
| 2 | `+0x1C` | `AudioMTKFilter::setParameter2Sub()` |
| 3 | — | error/log path |
| 4 | `+0x14` | `AudioMTKFilter::setParameter()` |

Selector 4 is valid; it is not the default/error case. This distinction is
important because `AudioALSAStreamManager::UpdateACFHCF()` uses manager
selectors 0, 1 and 4. The optional slot `+0x18` remains null in the default
manager initialization recovered above, so selector 1 is skipped unless
another initialization/writer populates that slot. See
[MT8163_ACF_HCF_CONTROL_PATH.md](MT8163_ACF_HCF_CONTROL_PATH.md).

These are internal HAL parameter selectors, not Android output-device masks.

At the next layer, `AudioMTKFilter::setParameter()` forwards the 1260-byte
custom-filter payload to its inner `MtkAudioLoud` object using inner selector
6. `AudioMTKFilter::setParameter2Sub()` forwards the same payload using
inner selector 8. Both methods also reapply the wrapper's stored configuration
values through inner selectors 1, 2 and 3, then finalize the update. The
wrapper's `process()` calls the inner object's processing vtable entry with
the byte-oriented input/output buffers.

`AudioMTKFilter::process()` at `0x9D7D8` takes the wrapper mutex at
`+0x24`, checks the inner object pointer at `+0x1C` and active flag at
`+0x20`, then invokes the inner object's vtable entry at `+0x14`. It
releases the mutex after the call and compares a global state snapshot; if
that value changed during processing, the wrapper returns zero rather than
the produced byte count. The meaning of that global guard is not yet
identified.

The stock vendor wrapper therefore uses a mutex around the filter call.
Do not copy this synchronization design into the custom PCM callback: the
project's own real-time `process()` path should remain allocation-free and
lock-free.


## 6. Integration implications

1. The MTK filter manager is created and registered per output-stream setup,
   then attached to the selected playback handler before its processing path.
2. The Normal handler's `doPostProcessing()` uses the attached manager only
   when its own pointer, stream-attribute bypass flags and scratch-capacity
   checks pass.
3. The manager selects different internal filters based on the output-device
   mask. Do not replace this routing with a single global DSP assumption.
4. The standard speaker route reaches the speaker branch after the Normal
   handler's stereo-to-mono fold; see
   [MT8163_NORMAL_SPEAKER_PCM_FORMAT.md](MT8163_NORMAL_SPEAKER_PCM_FORMAT.md).
5. This HAL code does not establish four-channel PCM or the board's
   physical connection to CD3313. The CD3313 datasheet does establish four
   analog outputs; whether all four are wired and used on this board remains
   a hardware/DT/mixer question. See [CD3313_REGISTER_IMAGE.md](CD3313_REGISTER_IMAGE.md).

No HAL binary, kernel module, mixer state, GPIO, I²S configuration, or live
head-unit state was modified.
