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

## Additional binary-level transport findings (2026-11-11)

The extracted `audiocmdservice_atci` binary was rechecked from the successful `Extract files.zip` artifact (workflow run [42885005088](https://github.com/14march93/mt8163_audio_engine/actions/runs/42885005088)).

Confirmed from its ELF dynamic symbol table and the matching init file:

- The init service is declared as `service audio-daemon /vendor/bin/audiocmdservice_atci`, with `disabled` and `oneshot`; it is not configured as an always-running boot service.
- Init creates the named local stream socket `atci-audio`, mode `0666`, owner/group `system`.
- The executable obtains that socket through `android_get_control_socket`, then uses the socket-server primitives `listen`, `accept`, `select`, and receive/send functions to handle ATCI clients.
- The executable imports `android::ProcessState::initWithDriver(char const*)` and `startThreadPool()`; its strings identify `/dev/vndbinder`. This is a vendor-Binder client path in addition to the local ATCI socket.
- It imports `android::MTKPrimaryDevicesHalClient::getInstance()`, `android::MtkAudioLoud::getBesSoundVer()`, `getAudioCompFltCustParamFromStorage()`, and `setAudioCompFltCustParamToStorage()`.
- It has no direct dynamic import of `ioctl()`, and the executable contains no `/dev/audiomux` or `cd3313` string.

The lack of an imported `ioctl()` is not proof that no indirect ioctl can occur inside one of its shared-library dependencies. It does, however, reinforce the separation already documented: this executable is an ATCI/vendor-Binder-to-MTK-HAL parameter service, not an identified direct CD3313 device client.

### Consequence for hidden API recovery

The next recovery target is not just `framework.jar`. The current archive is also missing all three direct service dependencies:

1. `libaudiocompensationfilter_vendor.so`
2. `libaudiocomponentengine_vendor.so`
3. `libaudioprimarydevicehalifclient.so`

For the HEQ-facing hidden `android.audio.AudioEffect` implementation, obtain the matching framework JAR (and any companion framework/vendor JAR containing that class), plus the associated native library or Binder service implementation. The service binary alone does not reveal the Java API's method-to-command mapping or argument scaling.

Do not map HEQ methods to driver commands based only on similar names or matching four-speaker argument counts. The Java wrapper, native transport, and driver payload layout must be correlated end-to-end before enabling writes.
