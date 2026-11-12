# Factory artifact replacement map

Date: 2026-11-12  
Target: MT8163 / Android 9 / ARM32 / vendor kernel 4.9.117  
Baseline: `14vvp93/public_calculations`, `firmware/manifests/mt8163-extracted.json`  
Baseline inventory: 1,386 entries; 578,175,653 bytes  
Status: initial path/hash inventory; not an installation package

## 1. Naming rule

Every replacement must retain the exact factory basename and partition-relative path recorded below. The path in the extraction manifest is mapped by removing the `firmware/extracted/` prefix. Example:

- Manifest: `firmware/extracted/vendor/lib/hw/audio.primary.mt8163.so`
- Factory destination: `/vendor/lib/hw/audio.primary.mt8163.so`
- Replacement output basename: `audio.primary.mt8163.so`

Project names such as `mt8163_audio_core` are allowed for source directories, CMake targets and intermediate static libraries only. They are not valid substitute names for a factory runtime artifact.

A matching name/path is necessary but not sufficient. Each final binary must also satisfy the original ABI, ELF class/machine, SONAME, symbol, dependency, linker namespace, init/service and permission contracts.

## 2. Confirmed factory artifacts

Hashes and sizes below are from the extracted-firmware manifest, not recomputed from a live device. These values are provenance anchors for the supplied firmware dump.

| Factory destination | Size | SHA-256 | Role / replacement disposition |
|---|---:|---|---|
| `/vendor/lib/hw/audio.primary.mt8163.so` | 1,671,852 | `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4` | **Primary replacement target.** Final primary HAL must use this exact basename and path. AudioCore is expected to be linked internally, not shipped as a second HAL. |
| `/system/lib/libaudioflinger.so` | 343,712 | `cc9847a03838d53342d617c683700ccb3e68cdac067352584f04a15a6ae8a3e3` | Contains vendor-specific device-control references. Audit every hook and framework contract; replace only if necessary to remove duplicate ownership, preserving framework ABI. |
| `/system/lib/libaudiopolicymanagerdefault.so` | 321,036 | `0ddcaabf015c96ce96430159edc0795e6dabc516ca0cf2afae11aababb09fa2d` | Vendor policy manager with direct device-control references. Replacement/compatibility adapter candidate; preserve Android policy contracts. |
| `/system/lib/libaudiopolicycustomextensions.so` | 113,564 | `9f4b6940d241a4bc6517214f1fc3fd0b3d69964f54aaa04e5ceb836d47d04afb` | Vendor policy extension; inspect exported ABI and callers before deciding full replacement versus redirection. |
| `/system/lib/libcarservices_jni.so` | 6,092,616 | `ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f` | Contains ExtAudioMuxer and car-status native paths. Must no longer independently own CD3313 state after cutover. Prefer a factory-name compatibility replacement/shim if Java contracts require it. |
| `/vendor/lib/libaudioprimarydevicehalifclient.so` | 41,268 | `b5e805b6cf20d995babb730f70b6287d4d4ff0c7168c189de326d05e8d58c22c` | HAL client/control bridge. Determine whether it remains compatible with replacement HAL control transport. |
| `/vendor/android.hardware.audio@4.0-service-mediatek` | 10,840 | `1f2848726bb752eb291c5abe583144e6cc4f4b49047b38750f2af21b96e9a827` | Audio HIDL service executable. Preserve service identity and init contract if retained; do not add a parallel service under a project name. |
| `/vendor/android.hardware.audio@4.0-service-mediatek.rc` | 907 | `20089785ff5d27955f2ce22314804b78bad89b0bb00f6c125b39330a95b3fe7f` | Init service declaration paired with the HIDL service. Any replacement must preserve expected service registration and lifecycle. |
| `/vendor/audiocmdservice_atci` | 54,136 | `302eee8562067a54d4b9e65ef982a100b94b36be6f73a993b9e01e059421a566` | Vendor ACF/HCF tuning command service. Migrate its tuning ownership or provide a compatible factory-name endpoint before retiring it. |
| `/vendor/audiocmdservice_atci.rc` | 181 | `4c38c958052e71915cb5e35037faf3b44c13a5332ecf65f2975d5f48c3621750` | Init declaration paired with audiocmdservice_atci. Keep/remove only as part of an explicit service migration. |
| `/vendor/lib/modules/cd3313.ko` | 188,520 | `6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594` | Factory CD3313 transport module. **Do not replace/build yet**: exact kernel source, config, toolchain and module ABI are not confirmed. |
| `/vendor/lib/modules/carstatus.ko` | 188,388 | `7d25bc2d3cf0b6de2823336189acb0c3ffc2fb8c29e889a06396683eb6cebe54` | Factory vehicle-status module. Preserve its MCU/vehicle ABI unless a separately validated replacement owns every signal. |
| `/vendor/lib/modules/ak7604.ko` | 293,740 | `be362f5c5bc448f396bffbc77c460d40003bb496deb0311424c4d97bc2d54ba1` | Alternative DSP module present in firmware. Do not load or replace experimentally; physical chip/binding is unknown. |
| `/vendor/lib/modules/fy7604.ko` | 282,016 | `3a09acda96db7ab89853c37276d2bef37e3c112262bb854475baa04b31046fa5` | Alternative DSP module present in firmware. Do not load or replace experimentally; physical chip/binding is unknown. |
| `/vendor/audio_policy_configuration.xml` | 11,252 | `1c7964434ee57913ebfecd0a98203557da19859b3eeb35bb8e1b6f48d7ad3146` | Vendor audio policy configuration. If changed, retain exact filename/path and validate all ports, profiles, devices and patches. |
| `/vendor/audio_effects.xml` | 5,832 | `61c6fe7cdd977c32b0014bdf1b07277b1cb7bebe1369b8907660fdee72d639a3` | Vendor effect registration. Keep factory identity; remove/replace entries only when equivalent behavior and loading contracts are verified. |
| `/vendor/audio_device.xml` | 8,407 | `eb3b490a033e26c00514c3a9b54f5ebf5bcc965e9839d746bc4f7a67bcaef56a` | Vendor audio device configuration. Preserve schema and referenced names. |
| `/vendor/AudioParamOptions.xml` | 2,855 | `e3f1827308fdfb0fbe82f2ce68d0604ced585f053818e2d2500196e2d2cef574` | Vendor audio tuning parameters. A byte-identical copy also exists under `/vendor/audio_param/AudioParamOptions.xml`; keep both paths until path-specific dependency checks prove otherwise. |
| `/system/audio_policy_configuration.xml` | 11,830 | `66080ee7be339d8e4d7893b02bef2d5067dfa42cb6aec489d9741df8e29830d7` | System audio policy configuration. Separate from the vendor copy; do not assume they are interchangeable. |
| `/system/audio_effects.conf` | 5,668 | `550ca26f659fa9062ad641db4ef8b2634f38f16eee49206b66249c4999592e3a` | Legacy system effect configuration. Keep its exact identity if the target framework still loads it. |

## 3. Replacement ownership plan

### First integration target

The first full replacement runtime artifact is:

`/vendor/lib/hw/audio.primary.mt8163.so`

It will contain the AudioCore state coordinator, PCM graph and source-specific route adapters as they become implemented. Internal static libraries can remain separately named in the build tree and be linked into this HAL. Do not create a new always-on audio daemon or a second primary HAL.

### Framework/vendor control clients

The primary HAL is not the only factory owner. The framework libraries, car-services JNI and vendor tuning service contain independent device-control paths. The migration plan must therefore:

1. redirect control requests into the one AudioCore owner;
2. preserve Java/Binder/native method contracts that existing clients still call;
3. prevent old clients from continuing to write CD3313 after cutover;
4. replace a factory binary under its original name only where required to remove the old owner or preserve a compatibility interface;
5. retire old services only after callers and init dependencies are accounted for.

A full replacement does not mean blindly replacing every Android framework library. Unmodified Android framework components remain in place when their public/vendor contracts can be preserved and their vendor-specific device ownership is safely redirected.

### Kernel modules

The module filenames and paths above are confirmed, but module replacement is blocked. Before building any `.ko`, recover the exact kernel source revision, `.config`, generated headers, compiler/toolchain, module symbol versions and vermagic. A successful generic kernel build is not a valid replacement. The current default plan is to keep the factory transport module while hardening the userspace broker; kernel changes are a separate, evidence-gated operation.

## 4. Required metadata for each future build artifact

The packaging manifest must record, for every output:

- factory destination path and exact basename;
- factory source SHA-256 and size;
- output SHA-256 and size;
- ELF class, machine, ARM EABI and SONAME (where applicable);
- exported and imported dynamic symbols;
- DT_NEEDED dependencies and linker namespace expectations;
- Android API/platform and C++ runtime assumptions;
- file mode, owner/group and SELinux label where recoverable;
- service/init/property/interface contracts for executables and configuration;
- kernel release, vermagic, symbol versions and config identity for modules;
- validation status: HOST VERIFIED, ANDROID BUILD VERIFIED, DEVICE VERIFIED;
- rollback source and hash.

Unknown metadata must be marked UNKNOWN, never guessed.

## 5. Packaging gates

- [ ] Every staged runtime artifact maps to one exact factory destination.
- [ ] No project-only basename is installed where Android/init expects a factory artifact.
- [ ] No unexpected extra audio HAL, daemon, library or kernel module is packaged.
- [ ] SONAME, ELF architecture, symbols, dependencies and permissions are checked.
- [ ] Both system and vendor policy/configuration files are tracked independently.
- [ ] Duplicate-content files remain at their original paths unless path-level dependency analysis authorizes a change.
- [ ] Original factory files and hashes are preserved for rollback.
- [ ] No image is assembled while a replacement has only HOST VERIFIED status if its ABI requires Android build verification.
- [ ] No hardware cutover is claimed before DEVICE VERIFIED route-by-route tests.

## 6. Evidence boundary

The inventory above is based on the supplied extracted firmware manifest and static reverse-engineering reports. It does not prove that every listed module is loaded, every listed service is active, or that the physical board contains each candidate DSP. In particular, the runtime owner of `/dev/audiomux` remains unknown. Do not load alternative DSP modules or issue speculative device commands to fill this gap.

Related: [Factory audio ownership matrix](FACTORY_AUDIO_OWNERSHIP_MATRIX.md), [Unified Audio Core architecture](UNIFIED_AUDIO_CORE_ARCHITECTURE.md), [Android integration gate](ANDROID_AUDIO_INTEGRATION_GATE.md), [Phase 1 audit baseline](PHASE1_AUDIT_BASELINE.md).
