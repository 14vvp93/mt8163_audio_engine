# Primary Android 9 HAL ABI recovery procedure

Target: MT8163 / Android 9 / ARM32  
Factory artifact: `/vendor/lib/hw/audio.primary.mt8163.so`  
Status: inventory procedure; ABI implementation remains blocked on exact vendor headers and complete interface recovery

## 1. Preserve and identify the factory binary

Use the extracted factory copy, not a device path or a modified binary:

```sh
sha256sum firmware/extracted/vendor/lib/hw/audio.primary.mt8163.so
stat -c '%s bytes' firmware/extracted/vendor/lib/hw/audio.primary.mt8163.so
sh tools/audit_primary_hal_binary.sh firmware/extracted/vendor/lib/hw/audio.primary.mt8163.so
```

Record the complete output with the firmware manifest. Do not strip, patch, relink or overwrite the factory artifact.

## 2. Recover the module entry points

Inspect the dynamic symbol table and relocations for:

- `HAL_MODULE_INFO_SYM` and the actual module object;
- module/device open and close functions;
- exported `audio_hw_device` methods;
- output/input stream methods;
- patch and device connection methods;
- any vendor extension symbols used by AudioPolicy or vendor services.

A missing symbol match is not proof that an entry point is absent: it may be hidden, stripped, reached through a function pointer, or exposed under a different symbol name. Inspect relocations, strings, disassembly and call sites before concluding.

## 3. Freeze the Android 9 structure contract

Use the exact Android 9 platform headers and vendor build headers that match the firmware. Record:

- structure sizes, alignment and field offsets for `hw_module_t`, `hw_device_t`, `audio_hw_device`, `audio_stream`, `audio_stream_out`, and `audio_stream_in`;
- API version fields and module/device tags;
- callback signatures and constness;
- required methods and optional methods;
- stream format/channel/rate negotiation;
- buffer-size and frame-count semantics;
- standby, drain, flush, presentation position and latency behavior;
- error-code conventions and ownership of returned buffers.

Do not copy structure definitions from a newer Android release and assume binary compatibility.

## 4. Recover dynamic linker requirements

Capture:

- SONAME;
- complete DT_NEEDED list;
- RPATH/RUNPATH, if present;
- undefined dynamic symbols;
- symbol version requirements;
- ARM attributes, EABI flags and build ID;
- vendor linker namespace and partition search-path assumptions.

The replacement must be built for the same target ABI and must not rely on unavailable host or newer-platform libraries.

## 5. Map lifecycle and routing

Trace each public HAL method into the recovered MTK implementation and record:

- output/input stream creation and teardown;
- primary, deep-buffer, direct, offload and MMAP streams if present;
- speaker, wired, AUX, HDMI, FM, Bluetooth A2DP and SCO routes;
- voice-call and microphone paths;
- audio patches and device connection changes;
- AudioPolicy and vendor service dependencies;
- all side effects involving `/dev/audiomux`, `/dev/carstatus`, ALSA and DSP controls.

Preserve HFP/SCO bidirectional audio and voice-call behavior. Do not route it through the music PCM path.

## 6. Integration gate

The replacement is not ready until the module ABI, callbacks, linker dependencies, stream lifecycle and routing matrix are complete. The final library must use the factory basename and destination:

`/vendor/lib/hw/audio.primary.mt8163.so`

AudioCore is an internal implementation component, not a separately installed competing audio HAL. Keep factory binaries untouched and provide a rollback copy and hash before any controlled device experiment.

## Validation labels

- `HOST VERIFIED`: host-side source, static inventory and tests.
- `ANDROID BUILD VERIFIED`: compiled against the pinned Android 9 vendor ABI and ARM32 toolchain.
- `DEVICE VERIFIED`: observed on the actual MT8163 target under a controlled test.

A successful ELF inventory is not an Android HAL implementation or device verification.
