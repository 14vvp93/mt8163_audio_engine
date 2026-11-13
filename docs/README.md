# MT8163 Audio Engine — Documentation Index

> **Canonical execution plan:** [Project Roadmap](PROJECT_ROADMAP.md). Use it for priorities, current status, gates and next actions. The older [Beta 0.1 roadmap](archive/PROJECT_ROADMAP_BETA_0.1.md) is retained only as a historical record.

Use this page as the navigation map for the project. The documents remain grouped by subject here; source files and research inputs are stored separately from buildable code.

## Project direction and status

- [Project Roadmap](PROJECT_ROADMAP.md) — single source of truth for product scope, repository baseline, priorities, implementation status, acceptance gates and release definition.

- [Repository migration and final ownership](REPOSITORY_MIGRATION.md) — portability rules, transfer scope and cutover checklist.
- [Repository boundaries and public compute architecture](REPOSITORY_BOUNDARIES_AND_PUBLIC_COMPUTE.md) — authoritative ownership split, workflow/data flow, security rules and branch policy.
- [Public compute worker](PUBLIC_COMPUTE_WORKER.md) — repository ownership, worker invocation and artifact handoff.
- [Firmware dataset migration](public-compute/FIRMWARE_DATASET_LAYOUT.md) — public firmware layout, verified transfer and archive deletion decision.
- [Public compute operating procedures](public-compute/WORKFLOWS.md) — workflow catalog and execution rules.
- [Public firmware compute scripts](public-compute/COMPUTE_SCRIPTS.md) — inventory, safe extraction, audio-stack analysis and HEQ verification tools.

- [GitHub Actions workflow audit](repository-audit/WORKFLOW_AUDIT.md) — workflow inventory, branch pinning and follow-up review.

- [Repository structure and file-placement rules](REPOSITORY_STRUCTURE.md) — canonical map of the repository and cleanup conventions.

- [Product goal and replacement scope](MT8163_AUDIO_ENGINE_GOAL.md) — agreed goal: one integrated engine replacing the factory vendor audio implementation.
- [Phase 1 audit baseline](PHASE1_AUDIT_BASELINE.md) — consolidated firmware, framework and hardware findings.
- [Factory audio ownership matrix](FACTORY_AUDIO_OWNERSHIP_MATRIX.md) — factory control/data-plane owners, mute arbitration and replacement responsibilities.
- [Factory artifact replacement map](FACTORY_ARTIFACT_REPLACEMENT_MAP.md) — confirmed factory paths, basenames, SHA-256 baselines and replacement dispositions.
- [Unified Audio Core architecture](UNIFIED_AUDIO_CORE_ARCHITECTURE.md) — single state authority, module contracts, source/route model, mute arbitration and migration gates.
- [Factory defect and risk register](FACTORY_DEFECTS.md) — confirmed static defects, unresolved cross-stack risks and closure criteria.
- [Project status](PROJECT_STATUS.md) — concise current snapshot; execution priorities remain in [Project Roadmap](PROJECT_ROADMAP.md).
- [Repository cleanup report](repository-audit/REPOSITORY_CLEANUP_REPORT.md) — repository layout, archive handling and cleanup decisions.
- [Archive inventory](repository-audit/ARCHIVE_INVENTORY.md) — complete extraction results and file-type counts.
- [Duplicate analysis](repository-audit/DUPLICATE_ANALYSIS.md) — byte-identical files and redundant source archive findings.
- [GitHub Actions workflow audit](repository-audit/WORKFLOW_AUDIT.md) — retained workflows, cleanup decisions and verified runs.
- [Firmware binary analysis summary](repository-audit/FIRMWARE_ANALYSIS_SUMMARY.md) — framework device-node hooks, alternative DSP modules, carstatus and HFP findings.

## CD3313EO physical IC and CD3313KO Linux driver

- [Protocol reconstruction](CD3313_PROTOCOL_RECONSTRUCTION.md)
- [Character-device read/write ABI](CD3313_CHAR_DEVICE_ABI.md)
- [ioctl and event-queue ABI](CD3313_IOCTL_ABI.md)
- [Command dispatch map](CD3313_COMMAND_DISPATCH.md)
- [Register image and datasheet correlation](CD3313_REGISTER_IMAGE.md)
- [Static driver risk audit](CD3313_DRIVER_RISK_AUDIT.md)
- [Hidden AudioEffect API and Binder route](CD3313_HIDDEN_AUDIOEFFECT_API.md)
- [Userspace client scan](CD3313_USERSPACE_CLIENT_SCAN.md)
- [Native JNI search audit](CD3313_NATIVE_JNI_SEARCH_AUDIT.md)
- [Native provider scan](CD3313_NATIVE_PROVIDER_SCAN.md)
- [API recovery requirements](CD3313_API_RECOVERY_REQUIREMENTS.md)
- [Reverse-engineering notes](CD3313_REVERSE_ENGINEERING.md)
- [CD3313 driver rewrite plan](../kernel/cd3313/README.md)
- [CD3313 host emulator](../cd3313-emulator/README.md)

## MT8163 platform, HAL and vendor stack

- [Primary Audio HAL analysis](MT8163_AUDIO_HAL_ANALYSIS.md)
- [Android 9 ARM32 primary HAL ABI baseline](ANDROID9_ARM32_PRIMARY_HAL_ABI_BASELINE.md) — pinned PPR1.180610.011 / AOSP r3 reference, recovered factory device/stream wrappers, vtables and remaining anomalies.
- [ARM32 ABI layout mirror](../audio-hal-abi/include/mt8163_android9_abi_layout32.h) — compile-time 32-bit layout model for factory module, device and stream wrappers.
- [Audio HAL reconstruction](MT8163_AUDIO_HAL_RECONSTRUCTION.md)
- [Vendor audio DSP stack](MT8163_VENDOR_AUDIO_DSP_STACK.md)
- [MTK audio lifecycle](MT8163_MTK_AUDIO_LIFECYCLE.md)
- [Filter-manager lifecycle and route selection](MT8163_MTK_FILTER_MANAGER_LIFECYCLE.md)
- [MTK process call-site audit](MT8163_MTK_PROCESS_CALLSITE_AUDIT.md)
- [Output-device dispatch audit](MT8163_OUTPUT_DEVICE_DISPATCH_AUDIT.md)
- [Normal speaker PCM format and mono-fold audit](MT8163_NORMAL_SPEAKER_PCM_FORMAT.md)
- [MTK process wrapper](MT8163_MTK_AUDIO_PROCESS_WRAPPER.md)
- [ACF/HCF control path](MT8163_ACF_HCF_CONTROL_PATH.md)
- [ACF/HCF structure reconstruction](MT8163_ACF_STRUCTURE_RECONSTRUCTION.md)
- [ATCI audio command service](MTK_AUDIO_COMMAND_SERVICE.md)

## Sources, FM, Bluetooth and vehicle integration

- [Audio source coverage](MT8163_AUDIO_SOURCE_COVERAGE.md)
- [FM routing matrix](MT8163_FM_ROUTING_MATRIX.md)
- [FM software route plan](FM_SOFTWARE_ROUTE_PLAN.md)
- [MT6627 FM driver ioctl ABI](MT6627_FM_DRIVER_ABI.md)
- [Carservices Java audio flow](CARSERVICES_JAVA_AUDIO_FLOW.md)
- [Carstatus MCU service ABI](CARSTATUS_MCU_SERVICE_ABI.md)

## Android integration and DSP

- [Android audio integration gate](ANDROID_AUDIO_INTEGRATION_GATE.md)
- [Android 9 AudioFlinger stereo insertion candidate](ANDROID9_AUDIOFLINGER_STEREO_INSERTION.md)
- [Android 9 stereo Effect Library](ANDROID9_STEREO_EFFECT_LIBRARY.md)
- [DSP insertion-point decision](MT8163_DSP_INSERTION_POINT_DECISION.md)
- [Android sound profiles](ANDROID_SOUND_PROFILES.md)
- [HEQ APK analysis](HEQ_APK_ANALYSIS.md)
- [HEQ algorithm analysis](HEQ_ALGORITHM_ANALYSIS.md)
- [PCM DSP core](../pcm-dsp/README.md)

## Validation and performance

- [PCM DSP stability lab](PCM_DSP_STABILITY_LAB.md)
- [PCM DSP performance baseline](PCM_DSP_PERFORMANCE_BASELINE.md)

## Tools

- [ADB audio and Device Tree diagnostics collector](../tools/README.md)
- [Offline Markdown link checker](../scripts/repository/check_markdown_links.py)
- [Primary HAL ELF/ABI audit](../tools/audit_primary_hal_binary.sh)
- [Android APS2 relocation/vtable decoder](../tools/audit_android_packed_relocations.py)
- [HAL ABI layout test](../audio-hal-abi/CMakeLists.txt)

## Repository areas

- `.github/workflows/` — product CI, Android builds, host tests and entry points to public compute workers.
- `android-test/` — Android-side bring-up and read-only API probes.
- `cd3313-emulator/` — offline CD3313 protocol/state model and host tests.
- `kernel/cd3313/` — CD3313 rewrite plan and future kernel implementation.
- `pcm-dsp/` — PCM/DSP code, JNI bridge, tests and benchmarks.
- `audio-core/` — unified AudioCore state primitives, mute arbitration, compatibility mapping and host tests.
- Firmware data — stored in `14vvp93/public_calculations/firmware/`; not duplicated in this private repository.
- `scripts/repository/` — private repository-maintenance utilities.
- `tools/` — diagnostic and reverse-engineering helper scripts.
