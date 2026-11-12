# MT8163 read-only diagnostic collection

Run from an Android shell on the target head unit:

```sh
sh tools/collect_mt8163_audio_diagnostics.sh
```

The script only reads properties, procfs, sysfs metadata, device-node metadata and kernel logs. It does not open PCM devices, send ioctls, write sysfs, or load/unload modules. Run it as the ordinary shell user first; permission-denied sections are useful evidence and should not be bypassed by changing device security settings.

Send the complete output as text. Before sharing, redact serial numbers, MAC addresses, VINs, phone numbers, Wi-Fi identifiers and any other personal/device identifiers. Keep ALSA card/device names, driver names, DSP property, module names and relevant audio log lines intact.

A successful collection is `DEVICE VERIFIED` only for the read-only facts actually returned by that device. It does not prove audible routing or safe hardware writes.
