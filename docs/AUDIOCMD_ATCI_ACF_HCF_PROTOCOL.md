# audiocmdservice_atci ACF/HCF parameter protocol

## Scope and source identity

Analyzed file: `vendor/audiocmdservice_atci` from the supplied
`extracted-files.zip`.

- SHA-256: `302eee8562067a54d4b9e65ef982a100b94b36be6f73a993b9e01e059421a566`
- ELF: ARM32, Android/Bionic, executable with Thumb-2 application code.
- ELF Build ID: `35659649e2b65bda1dfa22379f80d86c`
- The ELF contains a `.gnu_debugdata` mini-debug image. Its symbol table restores
  the names and ranges of the ACF/HCF handlers used below.

This is static binary analysis. No ATCI command was sent to a live unit and no
NVRAM or HAL parameter was changed.

## Linked vendor libraries

The ELF's `DT_NEEDED` table confirms these audio-specific dependencies:

- `libaudiocompensationfilter_vendor.so` — supplies the imported custom-filter
  NVRAM read/write functions.
- `libaudiocomponentengine_vendor.so` — part of the vendor audio component
  stack.
- `libaudioprimarydevicehalifclient.so` — supplies
  `MTKPrimaryDevicesHalClient::getInstance()`, used for the HAL notification
  path.

The service also depends on `libcutils.so`, `libutils.so`,
`libbinder.so`, and `liblog.so`. These dependencies reinforce that this
is a vendor parameter-storage and HAL-client bridge, not a direct CD3313
character-device client.

## 1. Recovered functions

The mini-debug symbol table identifies:

| Function | Address | Size |
|---|---:|---:|
| `DoCustACFHCFParam(AudioCmdParam&, void*, AudioCmdHandler*)` | `0x3109` | 268 B |
| `AudioCmdHandler::DLCustAudioFLTParamToNV(char*, AudioFltTunningType)` | `0x49BD` | 644 B |
| `AudioCmdHandler::ULCustAudioFLTParamFromNV(char*, int*, AudioFltTunningType)` | `0x4C41` | 492 B |
| `AudioCmdHandler::GetAudioData(char const*, unsigned int, void*)` | `0x34B5` | 468 B |
| `AudioCmdHandler::SetAudioData(int, unsigned int, void*)` | `0x36B9` | 444 B |
| `android::Base64_Decode(...)` | `0x6451` | 212 B |
| `android::Base64_Encode(...)` | `0x6349` | 204 B |

The service's command strings include `MM+EACF` and `MM+EHCF`. The
`DoCustACFHCFParam` handler dispatches the download-to-NVRAM path or the
upload-from-NVRAM path according to the command parameter structure.

## 2. Tuning-type and NVRAM-ID mapping

The global `AudioCompFltType_tMapID` contains five 32-bit values:

`[0, 1, 4, 5, 6]`

The corresponding `strFLTID` table is:

| Service tuning type | Name | NVRAM / HAL custom-filter ID |
|---:|---|---:|
| 0 | `ACF` | 0 |
| 1 | `HCF` | 1 |
| 2 | `ACF-SUB` | 4 |
| 3 | `Music-DRC` | 5 |
| 4 | `RingTone-DRC` | 6 |

The separate `strUpdateFLTID` table contains the exact notification strings:

- `UpdateACFHCFParameters=0`
- `UpdateACFHCFParameters=1`
- `UpdateACFHCFParameters=2`
- `UpdateACFHCFParameters=3`
- `UpdateACFHCFParameters=4`

The service tuning-type index is not itself the NVRAM ID. The mapping table
must be applied; in particular, service type 2 maps to ID 4.

## 3. Download path: ATCI payload to NVRAM and HAL notification

`DLCustAudioFLTParamToNV()` performs the following operations:

1. Rejects tuning-type values outside 0..4.
2. Maps the service tuning type through `AudioCompFltType_tMapID`.
3. Allocates a `0x4EC`-byte parameter buffer (1260 bytes).
4. Calls the imported
   `getAudioCompFltCustParamFromStorage(mapped_id, buffer, ...)` API to
   retrieve the current stored block; it expects a returned size of
   `0x4EC`.
5. Base64-decodes the incoming ATCI data. The decoded record begins with a
   32-bit length field that must equal `0x4EC`, followed by the 1260-byte
   payload.
6. Reads the current BES sound version using
   `MtkAudioLoud::getBesSoundVer()` and updates the version word at the end
   of the decoded payload (payload offset `0x4E8`).
7. Copies the `0x4EC)-byte payload into the parameter buffer and calls the
   imported `setAudioCompFltCustParamToStorage(mapped_id, buffer, 0x4EC)`.
8. Builds the matching `UpdateACFHCFParameters=N` String8 and sends it through
   `MTKPrimaryDevicesHalClient` to notify the HAL-side parameter path.

The interworking veneers in this ELF resolve the storage calls to the
`getAudioCompFltCustParamFromStorage` and
`setAudioCompFltCustParamToStorage` dynamic imports. The update string is
constructed from the indexed `strUpdateFLTID` table, not from an invented
property name.

## 4. Upload path: NVRAM block back to ATCI

`ULCustAudioFLTParamFromNV()`:

1. validates the same tuning-type range and applies the same ID mapping;
2. allocates a `0x4EC)-byte buffer;
3. reads the stored block through
   `getAudioCompFltCustParamFromStorage()`;
4. refreshes the BES version word at payload offset `0x4E8`;
5. returns the block through the service's Base64 encoding path.

The upload routine is an NVRAM read/export path. It is not evidence that the
current live filter coefficients are read back from the PCM processing engine.

## 5. Correlation with the primary HAL

The primary HAL's
`AudioALSAStreamManager::UpdateACFHCF(int)` at `0x7F18C` handles update
selectors 0, 1 and 2:

| Update string suffix | HAL custom-filter type loaded | Manager selector |
|---:|---:|---:|
| 0 | `AudioCompFltType_t(0)` | 0 |
| 1 | `AudioCompFltType_t(1)` | 1 |
| 2 | `AudioCompFltType_t(4)` | 4 |

This aligns service types 0, 1 and 2 with the HAL's ACF, HCF and ACF-SUB
custom-filter IDs 0, 1 and 4 respectively.

The service also has type 3 and 4 strings (Music-DRC and RingTone-DRC), but
the inspected `UpdateACFHCF(int)` function skips selectors outside 0..2.
Their activation path is not established by this function and must be traced
separately.

The manager's `setParameter()` selector table was also recovered:
- selector 0 -> slot `+0x14`, `setParameter2Sub()`;
- selector 1 -> slot `+0x18`, `setParameter2Sub()`;
- selector 2 -> slot `+0x1C`, `setParameter2Sub()`;
- selector 3 -> error path;
- selector 4 -> slot `+0x14`, `setParameter()`.

The default manager `init()` path initializes slot `+0x14`, but does not
assign a non-null object to slot `+0x18`. Therefore the HCF update's manager
selector 1 appears to be skipped in the recovered default construction path.
This is a static inconsistency requiring confirmation against the complete
live firmware/configuration; do not claim that HCF is applied at runtime.

## 6. What this does and does not establish

Established:
- the real ATCI command family and ACF/HCF handler functions;
- the five service tuning types and NVRAM IDs;
- the 1260-byte parameter-block size and Base64 transport;
- the NVRAM storage APIs;
- the exact HAL notification key format;
- the mapping from service update selectors 0..2 to HAL custom-filter IDs
  0, 1 and 4.

Not established:
- the complete 1260-byte structure definition and coefficient units;
- the exact ACF/HCF parser schema or filter-bank layout;
- the live runtime values of `IsAudioSupportFeature(2/3)`;
- why the default manager's HCF slot `+0x18` is null;
- how Music-DRC and RingTone-DRC are activated;
- any direct CD3313 I²C/ioctl transport.

Do not construct or write an arbitrary 1260-byte payload. Recover the matching
custom-parameter library/parser structures first. No HAL binary, NVRAM, mixer,
GPIO, I²S, kernel module, or live head-unit state was modified.
