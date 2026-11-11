# MT8163 firmware library reference

## Source archive

- Original archive: `lib_bf57dda0-1874-4303-acf8-5302f3787609.zip`
- Intended repository layout: `mt8163/lib/`
- ZIP integrity: verified (all entries pass CRC test)
- Files: 325 (plus directory entries)
- Expanded size: 98,891,944 bytes
- ZIP size: 43,323,642 bytes
- SHA-256: `f49a65f429b125ebe8c36f3f7b710f09fccd87b1fa36bb3c7009b8349a861879`

## Audio-related components identified

| Path | Size (bytes) |
|---|---:|
| `lib/hw/audio.primary.mt8163.so` | 1,671,852 |
| `lib/hw/android.hardware.audio@4.0-impl-mediatek.so` | 191,752 |
| `lib/hw/android.hardware.audio.effect@4.0-impl.so` | 209,520 |
| `lib/libaudiocompensationfilter_vendor.so` | 19,716 |
| `lib/libaudiocomponentengine_vendor.so` | 40,796 |
| `lib/libaudiocustparam_vendor.so` | 36,284 |
| `lib/libaudiodcrflt_vendor.so` | 24,652 |
| `lib/libaudioprimarydevicehalifclient.so` | 41,268 |
| `lib/libaudiosetting.so` | 15,564 |
| `lib/libaudiotoolkit_vendor.so` | 19,704 |
| `lib/libaudio_param_parser-vnd.so` | 164,728 |
| `lib/libbessound_hd_mtk_vendor.so` | 71,356 |
| `lib/libmtklimiter_vendor.so` | 34,484 |
| `lib/libmtkshifter_vendor.so` | 18,100 |
| `lib/libwebrtc_audio_preprocessing.so` | 813,424 |
| `lib/modules/cd3313.ko` | 188,520 |

The archive also contains graphics, camera, radio, DRM, Bluetooth, kernel-module and other system libraries. It is a firmware reference dump, not an installable package.

## Repository storage note

This file records the archive inventory and checksum. The 43.3 MB ZIP itself is not embedded in this Git commit yet; it should be added under this `mt8163/lib/` directory as a binary release asset or through Git LFS rather than expanded into ordinary Git history. Do not install, replace, or load any firmware component based only on its filename.
