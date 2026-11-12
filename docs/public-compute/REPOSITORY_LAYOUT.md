# Repository layout

```text
.
├── .github/workflows/       # Manually dispatched compute workflows
├── firmware/
│   ├── archives/            # Original public firmware archives (immutable inputs)
│   ├── dumps/               # Standalone public firmware images/dumps
│   ├── extracted/            # Re-creatable derived trees; large output is not committed by default
│   └── manifests/            # Source and extracted-file SHA-256 inventories
├── analysis/
│   ├── elf/                  # ELF headers, symbols, dependencies, build IDs
│   ├── audio-hal/            # Audio HAL, JNI, AudioFlinger/Policy evidence
│   ├── cd3313/               # CD3313 driver, ioctl, I2C and ABI analysis
│   ├── pcm-dsp/              # PCM, mixer, DSP and effects
│   └── config/               # Audio XML/INI/property configuration
├── scripts/
│   ├── firmware/             # Safe inventory and deterministic extraction
│   ├── elf/                  # ELF analysis helpers
│   └── common/               # Shared hashing and reporting helpers
├── reports/                  # Reviewed, compact, durable analysis results
└── docs/                     # Policies, contracts and workflow documentation
```

## Storage boundaries

- Keep original archives under `firmware/archives/`; never edit them in place.
- Store standalone raw dumps under `firmware/dumps/` with provenance metadata.
- Store manifests in Git even when the corresponding extracted tree is too large.
- Do not commit full extracted trees by default. Recreate them from verified inputs.
- Commit small, reviewed reports. Large intermediate output belongs in Actions artifacts
  or an explicitly selected durable large-file backend.
- Do not duplicate canonical Audio Engine implementation code here.
