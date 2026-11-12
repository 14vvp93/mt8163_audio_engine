# Workflow catalog

## Public compute repository

| Workflow | Trigger | Purpose | Writes |
|---|---|---|---|
| `compute.yml` | Reusable call / manual dispatch / approval-marker push | Archive inventory, safe split-ZIP extraction, audio inventory | Actions artifact only |
| `import-public-firmware.yml` | Manual dispatch / approval marker on main | Import approved public firmware from `14vvp93/mt8163_audio_engine` | Commits approved firmware and SHA-256 manifest to public main |
| `worker-analyze-audio-hal.yml` | Reusable call | HAL ELF, symbols, relocations and XML audit | Actions artifact |
| `worker-analyze-cd3313-ioctl.yml` | Reusable call | CD3313 ioctl ABI and event-queue analysis | Actions artifact |
| `worker-analyze-cd3313.yml` | Reusable call | CD3313 kernel module static analysis | Actions artifact |
| `worker-analyze-firmware-images.yml` | Reusable call | Boot, recovery and DTBO image inspection | Actions artifact |
| `worker-analyze-firmware-libraries.yml` | Reusable call | Firmware library and vendor stack analysis | Actions artifacts |
| `worker-audit-reference-archives.yml` | Reusable call | Split archive reassembly, extraction and inventory | Actions artifacts only |
| `worker-decompile-heq.yml` | Reusable call | HEQ APK reassembly and JADX analysis | Actions artifact |
| `worker-firmware-audio-config-audit.yml` | Reusable call | Manifest, hash, XML and audio config audit | Actions artifact |
| `worker-scan-audiomux-clients.yml` | Reusable call | CD3313/audiomux userspace client scan | Actions artifact |
| `validate-infrastructure.yml` | Push / pull request | Validate worker scripts and infrastructure | No repository writes |

## Primary repository entry point

| Workflow | Trigger | Purpose | Writes |
|---|---|---|---|
| `run-public-compute.yml` | Manual dispatch | Call the reusable worker in `14vvp93/public_calculations` | Worker artifact attached to the caller run |

## Operating rules

- Use the primary repository as the operator-facing entry point.
- Dispatch expensive compute explicitly; do not run full firmware analysis for ordinary
  source-code edits.
- Validation is lightweight and must not unpack full firmware archives.
- The worker does not commit generated reports automatically.
- Artifacts are temporary; durable findings are reviewed before promotion.
- All workflows use least-privilege permissions and bounded runtime.
