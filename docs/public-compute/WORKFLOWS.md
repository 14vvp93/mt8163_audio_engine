# Workflow catalog

## Private project entry points

| Workflow | Trigger | Purpose |
|---|---|---|
| run-public-compute.yml | Manual dispatch in private project | Call compute.yml for inventory, verification or audio-stack inventory |
| run-firmware-analysis.yml | Manual dispatch in private project | Invoke one of the nine firmware-only analysis workers |

## Public reusable workers

| Workflow | Purpose | Output |
|---|---|---|
| compute.yml | Firmware inventory, SHA-256 verification and audio-stack inventory | Actions artifact |
| worker-analyze-audio-hal.yml | HAL ELF, symbols, relocations and disassembly | Actions artifact |
| worker-analyze-cd3313-ioctl.yml | CD3313 ioctl ABI and event-queue analysis | Actions artifact |
| worker-analyze-cd3313.yml | CD3313 kernel module static analysis | Actions artifact |
| worker-analyze-firmware-images.yml | Boot, recovery and DTBO inspection | Actions artifact |
| worker-analyze-firmware-libraries.yml | Firmware libraries, CompactDex/VDEX and board audio routing | Actions artifacts |
| worker-audit-firmware-integrity.yml | Extracted-tree SHA-256 verification and HEQ APK verification | Actions artifact |
| worker-decompile-heq.yml | HEQ APK reassembly and JADX analysis | Actions artifact |
| worker-firmware-audio-config-audit.yml | Firmware manifest, XML and audio configuration audit | Actions artifact |
| worker-scan-audiomux-clients.yml | CD3313/audiomux userspace-client scan | Actions artifact |
| validate-infrastructure.yml | Workflow YAML, Python compilation, tests and CLI checks | No repository writes |

## Operating rules

- Use the private project as the operator-facing entry point.
- Public workers read firmware only from their own repository checkout.
- Public workers do not check out private source code and do not require a private read token.
- Do not run full firmware analysis for ordinary product source edits.
- Workers do not push generated data into either repository.
- Artifacts are temporary; reviewed findings are promoted into private documentation.
