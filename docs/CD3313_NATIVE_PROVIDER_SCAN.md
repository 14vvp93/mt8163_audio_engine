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

Do not implement a direct `/dev/audiomux` client from the kernel driver's request layout alone. The exact userspace ioctl request word and framing remain unknown. The recovered driver ABI also has unsafe short-payload behavior for some commands, so any future adapter must enforce complete argument lengths and ranges before crossing the Binder/native boundary.
