# CD3313 native-provider collection and scan

## Why broaden the search

The currently supplied 325-file archive does not include the system-server native library or a native implementation containing the recovered `ExtAudioMuxer` method names. The previous scanner considered only `*.so` files. That is too narrow: a JNI provider may be registered by another ELF executable/library, while class references may also appear in optimized bytecode artifacts.

## Run on the complete extracted firmware

From the repository root on Windows PowerShell 5.1+:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_cd3313_native_provider.ps1 -Root D:\Desktop\MT8163
```

Optional output folder and maximum file size:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_cd3313_native_provider.ps1 -Root D:\Desktop\MT8163 -OutputDir .\CD3313_Native_Provider -MaxFileBytes 268435456
```

The script:
- scans all files under the extracted firmware root up to the configured size limit;
- identifies ELF files by their magic bytes, not by filename extension;
- searches for exact bridge markers (`SendCmdAudioMux`, `ExtAudioMuxer`, `/dev/audiomux`, etc.);
- records generic JNI markers only as supporting evidence;
- includes `libandroid_servers.so` by filename even if stripped;
- copies candidate files and records relative paths, sizes and SHA-256 hashes;
- creates `CD3313_Native_Provider.zip`.

The scan is read-only. It does not execute any firmware binary, open `/dev/audiomux`, issue ioctl calls, or change audio settings.

## How to interpret results

- **Strong marker match in an ELF**: inspect this file first; it may contain the bridge or device path.
- **ELF with only generic JNI markers**: candidate only. Many unrelated Android libraries register JNI methods.
- **VDEX/ODEX with Java class references**: corroborates Java-side usage, but does not identify the native provider.
- **No match**: not proof that no provider exists. The bridge may be statically linked, stripped/obfuscated, use a dynamically constructed path, or reside in a partition not included in the extracted root.

## Safety gate

Do not implement a direct `/dev/audiomux` client from the kernel driver's request layout alone. The expanded artifact has since recovered the native ioctl request word and framing; however, its `array_length + 1` size convention causes a one-word copy beyond the declared Java array. The extra word is unknown. The driver also has unsafe short-payload behavior for some commands, so any future adapter must enforce complete argument lengths and ranges before crossing the Binder/native boundary.


## 2026-11-11 expanded firmware artifact result — provider found

The earlier 325-file `files2` scan above was correctly limited to that archive, but it is no longer the latest evidence. The expanded `firmware-library-analysis` artifact (run
[43089987118](https://github.com/14march93/mt8163_audio_engine/actions/runs/43089987118))
reassembled the supplied `files`, `files2`, `lib`, and `vendor_lib` archives.
It contains the native provider at:

`lib/lib/libcarservices_jni.so`

Confirmed binary details:

- ELF32 ARM, EABI5, Android 28, stripped.
- Build ID: `bf391e545edd2e482b72cd8a431d1c59`.
- SHA-256: `ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f`.
- JNI registration class: `android/sourceservice/ExtAudioMuxer`.
- Native methods include `nativeInit` and `SendCmdAudioMux`.
- Imports include `__open_2`, `ioctl`, and `close`.
- Strings include `/dev/audiomux`, `ExtAudioMuxer`, and `SendCmdAudioMux`.

Static ARM disassembly confirms that `nativeInit()` probes `/dev/audiomux`
with read request `0x8080F000`, while `SendCmdAudioMux(int,int[])` opens
the same node write-only and calls `ioctl()`. The request formula and the
one-extra-word copy discrepancy are documented in
[CD3313_HIDDEN_AUDIOEFFECT_API.md](CD3313_HIDDEN_AUDIOEFFECT_API.md) and
[CD3313_IOCTL_ABI.md](CD3313_IOCTL_ABI.md).

The provider search objective is therefore **completed** for this supplied
firmware set. The remaining gap is runtime validation and safe handling of
the extra-word discrepancy, not locating the JNI library. No firmware binary
was executed and no live device operation was performed.
