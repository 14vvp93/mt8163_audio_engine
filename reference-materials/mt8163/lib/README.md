# MT8163 audio reference files

Curated selection from the user-supplied firmware `lib` archive. The original archive contained 325 files; this selection retains 26 audio-related binaries (4,632,788 bytes uncompressed) plus this README.

## Selected files

- `hw/audio.primary.mt8163.so` — primary MT8163 audio HAL.
- `hw/android.hardware.audio@4.0-impl-mediatek.so` — MediaTek HIDL audio implementation.
- `hw/android.hardware.audio.effect@4.0-impl.so` — HIDL audio-effects implementation.
- MediaTek audio/HAL support: `libaudiocompensationfilter_vendor.so`, `libaudiocomponentengine_vendor.so`, `libaudiocustparam_vendor.so`, `libaudiodcrflt_vendor.so`, `libaudioprimarydevicehalifclient.so`, `libaudiosetting.so`, `libaudiotoolkit_vendor.so`, `libaudio_param_parser-vnd.so`, `libbessound_hd_mtk_vendor.so`, `libmtklimiter_vendor.so`, `libmtkshifter_vendor.so`.
- Audio support/dependencies: `libblisrc_vendor.so`, `libspeech_enh_lib.so`, `libladder.so`, `libaedv.so`, `libtinycompress.so`, `libtinyxml.so`, `libeffects.so`.
- Bluetooth voice/audio: `libbluetooth_mtk_pure.so`, `libcvsd_mtk.so`, `libmsbc_mtk.so`.
- Audio HIDL interface: `vendor.mediatek.hardware.audio@4.1.so`.
- Voice preprocessing: `libwebrtc_audio_preprocessing.so`.

## Deliberately excluded

- `modules/cd3313.ko` — already stored at `reference-materials/kernel/cd3313.ko`; no duplicate.
- Camera, radio, graphics, DRM, and unrelated system libraries.
- Generic Android system dependencies absent from the supplied archive (including `libtinyalsa.so` and `libmedia_helper.so`).

The curated ZIP is provided separately as `mt8163-audio-required-files.zip` (SHA-256: `434c5fbce500ec680f5fc0058b3e6135e34e0c988274c6654cc8b4cdfef97280`). It preserves the source paths under `lib/`.

These binaries are firmware reference material for offline analysis only. Do not install, replace, or load them on a device without separate compatibility and safety verification.
