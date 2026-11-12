# Curated extracted firmware references

This directory contains the persistent, audio-relevant subset promoted from the complete archive extraction.

- system/ — Android system audio policy/configuration, audio-related system libraries, AudioInputSource and carservices compiled artifacts.
- vendor/ — the complete extracted vendor/lib tree, including HAL libraries, vendor dependencies, kernel modules, plus audio service/configuration files.
- manifest.json — source archive/member, destination, size and SHA-256 for every promoted file.

All archive sets were extracted and CRC-checked in GitHub Actions. The full extraction is available as a 30-day workflow artifact. This curated tree avoids committing unrelated Android system libraries, generic notification/ringtone assets, framework stub JARs, and the 161 MiB HEQ APK. Original archives remain the source of truth and are not modified.

Do not install or load these reference binaries on a device. They are read-only reverse-engineering inputs.
