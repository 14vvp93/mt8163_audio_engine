# files2 Archive Analysis

Date: 2026-11-11

## Acquisition and validation

The split archive `files2.zip.001`, `.002`, and `.003` was concatenated and validated with 7-Zip in GitHub Actions. Extraction completed successfully. The workflow artifact was downloaded and inspected locally.

- Workflow run: [42986021391](https://github.com/14march93/mt8163_audio_engine/actions/runs/42986021391)
- Artifact: `files2-extracted-and-inventory` (artifact ID `13838818786`)
- Artifact SHA-256: `b3083dbbd21a0167686b62c1d4d795a712a169ada572feb13470d5bc1f6709f3`
- Extracted inventory: 324 files.
- Archive tree is nested under `files2/files2/`.

## Relevant contents

The archive includes a subset of Android framework, system, and vendor files:

- `vendor/audio.primary.mt8163.so` — ELF32 ARM, Android 28, Build ID `f66eaf9e6ac950259f0c253be618d15a`.
- `vendor/audiocmdservice_atci` and its init rc.
- `vendor/audio_effects.xml`, `audio_policy.conf`, `audio_policy_configuration.xml`, `AudioParamOptions.xml`.
- `system/libem_audio_jni.so`.
- Framework JAR/ODEX/VDEX and system audio policy/effect files.

The primary HAL Build ID matches the HAL examined in the earlier reconstruction work; this archive therefore corroborates that same binary rather than providing a new HAL revision.

## Findings

1. The primary HAL's dynamic dependencies include `libaudiocomponentengine_vendor.so`, `libaudiosetting.so`, `libaudiotoolkit_vendor.so`, `libaudiodcrflt_vendor.so`, and other MediaTek libraries. These dependency files are not present anywhere in this archive.
2. The previously identified recovery targets `libaudiocompensationfilter_vendor.so` and `libaudioprimarydevicehalifclient.so` are also absent.
3. `libem_audio_jni.so` imports `android::AudioSystem::getParameters()` and `setParameters()`, and its JNI entry point is `JNI_OnLoad`. Its visible strings do not establish a CD3313-specific transport or API.
4. The archive contains no obvious CD3313-named library, device client, or framework API implementation.
5. The framework JARs in this dump are mostly tiny boot-classpath stubs; the substantive compiled framework material is represented by OAT/VDEX files. The Mediatek framework VDEX/OAT string scan did not reveal the hidden `android.audio.AudioEffect` CD3313 API implementation.
6. The vendor service and HAL material are useful corroborating references, but this archive does not close the missing native dependency/API gap needed to safely reconstruct the CD3313 control path.

## Integration implications

- Keep the existing safety gate: do not replace the stock HAL, inject guessed ioctl payloads, or map HEQ methods to CD3313 driver commands by name similarity.
- Continue seeking a matching full vendor library set, especially the HAL's unresolved MediaTek dependencies, plus the framework/vendor implementation behind the hidden audio API.
- Treat `libem_audio_jni.so` as a generic AudioSystem parameter bridge until its registered Java class/method table and call sites are recovered.

## Scope limitation

This is a partial filesystem dump (324 files), not a complete `/system` + `/vendor` image. Absence from this archive does not prove a component is absent from the head unit.


## Additional audio configuration findings

A second pass over the extracted XML files confirms:

- `vendor/AudioParamOptions.xml` and `vendor/audio_param/AudioParamOptions.xml` both select `MTK_AUDIO_SPEAKER_PATH=int_spk_amp` for platform MT8163.
- The same options set `MTK_BESLOUDNESS_RUN_WITH_HAL=no` and `MTK_AURISYS_FRAMEWORK_SUPPORT=no`.
- `vendor/audio_device.xml` configures the two-in-one speaker path by switching `Audio_Speaker_class_Switch` between `CLASSAB` and `RECEIVER`; it separately configures voice and headset amplifier switches.
- No I²S playback sequence table or SmartPA parameter unit was found in this 324-file archive subset. No mixer_paths file or board-level I²S/TDM routing description is included.

The configured speaker-path value is `int_spk_amp`, not `smartpa`. The HAL's `initSmartPaAttribute()` compares this option with the literal `smartpa` before loading the SmartPA-specific attribute set, so the captured configuration does not select that SmartPA attribute-loading branch. This conclusion is limited to the captured XML and does not exclude runtime overrides or a different firmware build.
