# MT8163 audio control architecture — static baseline

Date: 2026-11-12  
Status: architecture map from imported firmware; implementation not started.

## 1. Observed control paths

```mermaid
flowchart TD
    AF[Android AudioFlinger] -->|/dev/audiomux| MX[External DSP control ABI]
    AP[AudioPolicyManager] -->|/dev/audiomux| MX
    JNI[ExtAudioMuxer JNI\nSendCmdAudioMux(int, int[])] -->|ioctl; one-word size mismatch| MX
    MX --> Q[Kernel delayed event queue]
    Q --> DSP[Selected I2C DSP driver]
    DSP --> CHIP[CD3313 / AK7604 / FY7604]

    HIDL[Audio HAL 4.0 service] --> HAL[audio.primary.mt8163.so]
    HAL --> PCM[TinyALSA PCM streams]
    HAL --> MIX[ALSA mixer controls]
    MIX --> XML[audio_device.xml]
    PCM --> AFE[MT8163 AFE / codec paths]

    CS[carstatus.ko] -->|car status and mute integration| AF
```

The diagram shows logical dependencies found in binaries and configuration. It does not claim that every optional DSP chip is physically populated or successfully probed.

### Android framework and HIDL

- `android.hardware.audio@4.0-service-mediatek` is the configured vendor HIDL service.
- `android.hardware.audio@4.0-impl-mediatek.so` supplies the HIDL implementation.
- `audio.primary.mt8163.so` contains the MediaTek primary audio HAL and TinyALSA/mixer integration.
- `libaudioflinger.so` and `libaudiopolicymanagerdefault.so` both reference `/dev/audiomux` and `/dev/carstatus`.
- The vendor AudioPolicy XML has a Bluetooth SCO input channel-mask inconsistency; the system XML's corresponding input entry is correct.

### PCM and mixer plane

The primary HAL contains distinct stream/provider classes for speaker/headphone playback, built-in and wired microphones, FM capture/playback, Bluetooth SCO/CVSD, echo reference, ANC and voice wakeup. It uses TinyALSA PCM streams and ALSA mixer controls.

The vendor `audio_device.xml` describes mixer paths and controls such as speaker-class switch, speaker/headphone amplifier, ADC switches, mic modes and sidetone. It parses as XML but contains stray text after some self-closing `kctl` nodes and one duplicated ADC-off control; these are preserved as source evidence.

### Mixer path inventory from `audio_device.xml`

The primary HAL contains `AudioALSADeviceParser` and `AudioALSADeviceConfigManager` components, matching the supplied mixer XML. The extracted paths include:

- Receiver: `Voice_Amp_Switch` on/off.
- Two-in-one speaker: `Audio_Speaker_class_Switch=CLASSAB` for turn-on and `RECEIVER` for turn-off.
- Headphone/speaker: `Headset_Speaker_Amp_Switch` on/off.
- Built-in mic variants: Mic1, Mic3, SingleMic, DualMic and BackMic, with ADC source, ADC switch and preamp selections.
- Wired headset microphone: ADC2/ADC3 source and preamp selections.
- Sidetone: `Audio_Sidetone_Switch` on/off.
- Four microphone bias/mode selector groups: ACC, DCC, DMIC and ECM variants.

The XML's `builtin_Mic_Mic3/turnoff` repeats `Audio_ADC_3_Switch=Off`. Other mic source/preamp combinations should be compared with the board wiring before porting; the XML alone is not enough to infer the physical mic topology.

### External DSP command plane

- The native class `android/sourceservice/ExtAudioMuxer` registers `SendCmdAudioMux(int, int[])`.
- Its JNI implementation opens/uses `/dev/audiomux` and constructs an ioctl request.
- The CD3313 vendor driver queues ioctl commands asynchronously, while its raw write operation sends directly to I2C.
- The `cmd_read` path reports cached software state, not chip readback.
- The JNI ioctl-size calculation advertises one more 32-bit word than the Java array contains.
- Kernel command validation is incomplete for several command IDs; the detailed findings are in the ABI audit.

## 2. DSP device selection and ownership

The base DTB embedded in `boot.img` has I2C controllers at `0x11007000`, `0x11008000` and `0x11009000` marked disabled. The single DTBO entry activates the buses; its `__fixups__` maps symbol `i2c2` to `fragment@11:target`, and that fragment overlays the third controller (`0x11009000`) with the following devices:

| Device-tree node | Compatible | I2C address | Status |
|---|---|---:|---|
| `cd3313@44` | `hcn,cd3313` | `0x44` | `okay` |
| `ak7604@1c` | `hcn,ak7604` | `0x1c` | `okay` |
| `fy7604@50` | `hcn,fy7604` | `0x50` | `okay` |

The init configuration inserts `carstatus.ko`, then `cd3313.ko`, `ak7604.ko` and `fy7604.ko`. Each DSP driver has a misc-device structure named `audiomux`; each probe checks `misc_register` and returns an error if registration fails. AK7604 and FY7604 also request overlapping GPIO lines.

This is a conditional ownership conflict: if multiple DSP devices respond and reach misc registration, they compete for the same misc-class device name. The first successful probe can prevent later probes. The shared name and DTBO nodes alone do not prove which physical chips are present; the actual probe log is still required.

The vendor CD3313 module at `vendor_lib/lib/modules/cd3313.ko` is the build loaded by init. It is a different, newer `publisher28` build from the standalone `firmware/dumps/mt8163/kernel/cd3313.ko` reference. The vendor build adds exact I2C transfer checks and retries; use it as the baseline for the target firmware.

## 3. Current control-plane risks

1. **Multiple framework clients:** AudioFlinger, AudioPolicy and ExtAudioMuxer JNI can issue commands to the same node.
2. **JNI/kernel ABI mismatch:** the JNI advertises one extra integer; the kernel copies it.
3. **Incomplete payload validation:** several ioctl commands read fields without checking the encoded payload length; command `0x15` reads four words but validates only a shorter length.
4. **Asynchronous success semantics:** ioctl success means queued, not applied to hardware.
5. **Cached state:** readback is not physical DSP readback.
6. **Raw write bypass:** direct I2C writes bypass the delayed event worker and can interleave with a multi-register command; file-position and NUL-termination handling also need correction.
7. **DSP ownership:** multiple drivers share the misc name `audiomux`; actual probe outcomes must be verified.
8. **Selector property:** `ro.hw.dsp.chip` is referenced by JNI and boot-carservices VDEX but is not set in the supplied property/init files.

## 4. Target architecture for the replacement engine

The replacement should have one state owner and explicit adapters rather than preserving the current multiple-writer design.

- **Android compatibility boundary:** preserve the HIDL 4.0 and Audio HAL contracts until all framework/vendor callers are mapped.
- **PCM backend:** own TinyALSA PCM open/close, stream parameters, route activation and mixer control transactions.
- **DSP backend:** select the actual chip from validated board information; expose one command API with a typed descriptor table and per-chip register adapters.
- **Single command executor:** serialize every DSP transaction, including diagnostics; no direct raw-write bypass.
- **State model:** track requested state, last successfully applied state, and observed/read-back state separately.
- **Completion contract:** synchronous result or an explicit transaction ID/completion event; never report queued as applied.
- **Validation:** validate command ID, exact payload size, ranges and chip capabilities before any work is queued.
- **Diagnostics:** expose structured errors and actual transport results without presenting cache values as hardware readback.

## 5. Next sequence

1. Obtain read-only probe logs for `cd3313`, `ak7604`, `fy7604` and `audiomux` to resolve which DSP driver owns the device node on the target unit.
2. Map the Java/JNI callers and all AudioFlinger/AudioPolicy ioctl command payloads.
3. Map the active PCM device names, ALSA mixer controls and mixer paths.
4. Build the compatibility contract and state model before replacing any vendor service.
5. Implement the DSP transport and PCM routing behind one engine-owned control plane.

## Scope

All findings are from imported files, ELF metadata, static disassembly and DTBO parsing. No live device was accessed, no module was loaded, and no hardware settings were changed.
