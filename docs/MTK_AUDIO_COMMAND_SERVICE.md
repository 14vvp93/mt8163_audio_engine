# MTK Audio Command Handler service analysis

## Binary and service configuration

- Executable: `vendor/audiocmdservice_atci`
- Format: ELF32 ARM EABI5, Android 28 target, stripped.
- Build ID: `35659649e2b65bda1dfa22379f80d86c`
- Init file: `vendor/audiocmdservice_atci.rc`
- Init declaration: `service audio-daemon /vendor/bin/audiocmdservice_atci`
- Class: `main`; user `system`; groups `system audio`; disabled; oneshot.
- Init creates a local stream socket named `atci-audio` with mode 666 and owner/group system.

The service is a vendor audio command handler, not the CD3313 kernel driver itself. Its executable strings identify an ATCI socket server and audio parameter commands.

## Confirmed audio filter responsibilities

The binary imports:
- `getAudioCompFltCustParamFromStorage`
- `setAudioCompFltCustParamToStorage`
- `android::MtkAudioLoud::getBesSoundVer()`
- `android::MTKPrimaryDevicesHalClient::getInstance()`

Its dynamic dependencies include:
- `libaudiocompensationfilter_vendor.so`
- `libaudiocomponentengine_vendor.so`
- `libaudioprimarydevicehalifclient.so`

The supplied archive does not include these three dependency libraries, so their internal implementations and exact ABI structures cannot yet be reconstructed from this material.

The service contains handler routines with names such as:
- `DLCustAudioFLTParamToNV()`
- `ULCustAudioFLTParamFromNV()`

These routines transfer audio filter parameter structures to/from persistent storage. Their log strings expose fields including:
- `bes_loudness_Sep_LR_Filter`
- `bes_loudness_WS_Gain_Max`
- `bes_loudness_WS_Gain_Min`
- `bes_loudness_Filter_First`
- `bes_loudness_Num_Bands`
- `bes_loudness_Flt_Bank_Order`
- `DRC_Delay`
- `Lim_Th`
- `Lim_Gn`
- `Lim_Const`
- `Lim_Delay`

This confirms that the MTK ACF/BES Loudness path carries a structured filter configuration with multiband/loudness and limiter parameters. It is not merely a simple 5-band EQ.

The command strings include `MM+EACF`, `MM+EHCF`, and `ACF-SUB`. The service also contains `UpdateACFHCFParameters=0..4`, indicating several update modes. Exact ATCI command grammar and parameter structure sizes are still unresolved; strings alone are not sufficient to safely synthesize messages.

## Service communication

The executable logs show:
- a local socket server named `atci-audio`;
- command receive and parse loop;
- forwarding of responses to an ATCI client;
- use of `MTKPrimaryDevicesHalClient` to communicate with the primary audio HAL.

This makes the service a likely bridge for factory/engineering audio tuning commands into the HAL's ACF/HCF parameter system. It is not yet proven to issue commands to `/dev/audiomux` or CD3313.

## CD3313 separation

No `/dev/audiomux` or `cd3313` strings were found in this executable. It also has no direct imported `ioctl()` symbol in its dynamic symbol table. Current evidence therefore does not connect this service to the CD3313 misc device.

Keep these interfaces separate:
1. ATCI audio command socket → MTK primary HAL client → ACF/HCF/BES parameter management.
2. CD3313 userspace ABI → `/dev/audiomux` → kernel driver → I²C device at address `0x44`.

A connection between them must be demonstrated from further binaries or source, not assumed.

## Implications for our implementation

- Do not emulate or reuse the ATCI protocol until its full framing and command grammar are reconstructed.
- Do not overwrite vendor NVRAM filter structures with guessed layouts.
- Preserve the existing MTK ACF/HCF path during early DSP development.
- Implement our DSP as an independent PCM engine; add a control adapter later, after the actual parameter ABI is known.
- If we eventually expose CD3313 controls, use a separate narrowly scoped native service/library with validated command structures and a strict allowlist.

## Next files needed to finish this branch

The three imported libraries listed above are missing from the supplied archive. They are the highest-value next artifacts because they should expose the filter parameter ABI, HAL client bridge, and BES Loudness implementation.
