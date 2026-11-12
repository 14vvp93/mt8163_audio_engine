# Public compute worker contract

## Ownership

`14vvp93/mt8163_audio_engine` is the private source of truth for product code, build definitions, tests and engineering documentation.

`14vvp93/public_calculations` is the public source of truth for approved firmware data and firmware-only heavy compute. It contains raw firmware dumps, the SHA-256-verified extracted tree, integrity manifests, reusable GitHub Actions workers and compute scripts. It does not contain product implementation code or project documentation.

## Invocation

The private project invokes reusable workflows in the public repository through:

- `run-public-compute.yml` → `compute.yml`
- `run-firmware-analysis.yml` → the nine firmware analysis workers

The public workflows check out their own public firmware dataset. They do not check out the private project and do not require a private repository token. Worker outputs are uploaded as Actions artifacts to the calling workflow run.

## Supported compute tasks

- `inventory` — hash and inventory extracted firmware and standalone raw dumps.
- `verify` — verify the canonical extracted dataset against its SHA-256 manifest.
- `all-light` — produce a firmware inventory without deep analysis.
- `audio-inventory` — verify firmware and generate Android audio-stack/DSP inventory.

Nine specialized workers perform HAL analysis, CD3313 ioctl/static analysis, firmware image inspection, library analysis, firmware integrity/HEQ verification, HEQ decompilation, audio configuration audit and audiomux client scanning.

## Data and outputs

- Inputs are read from `firmware/dumps/mt8163/`, `firmware/extracted/` and `firmware/manifests/`.
- The extracted tree contains 1,386 files; the manifest records per-file size and SHA-256.
- The original split ZIP archives were removed after the extracted tree was verified and committed.
- Large intermediate outputs and reports belong in Actions artifacts, not Git.
- No worker pushes generated files into either repository.

## Security and reliability

- Public firmware data, workflow logs and artifacts must contain no credentials, personal data or private signing material.
- Use read-only permissions and pin third-party Actions to reviewed full commit SHAs.
- Bound runtime and artifact retention.
- Keep project code and documentation private.
- Do not install firmware or write to a physical MT8163 device from these workflows.
