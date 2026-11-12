# Public firmware and compute worker

## Repository ownership

- `14vvp93/mt8163_audio_engine` is the private canonical project repository. It owns all product source code, build definitions, tests, engineering documentation, analysis decisions and project reports.
- `14vvp93/public_calculations` is the public firmware-data and compute repository. It owns approved firmware dumps, the verified extracted firmware tree, SHA-256 manifests, heavy analysis scripts, worker tests and GitHub Actions workflows.
- The public repository must not contain product implementation code or project documentation. The private repository must not contain firmware archives, dumps or extracted firmware trees.

## Data flow

1. Approved firmware data is maintained in `public_calculations/firmware/`.
2. Public compute workflows read firmware directly from that repository.
3. When a worker needs project code or private analysis helpers, it checks out the requested revision of `mt8163_audio_engine` using `SOURCE_REPO_READ_TOKEN` with Contents: Read-only.
4. The worker combines private source code with public firmware data only in its ephemeral runner workspace.
5. Results are uploaded as GitHub Actions artifacts. The worker does not push firmware, reports or generated data into the private repository.

The token is a read-only credential. Never print it, write it to files, or pass it as an ordinary workflow input.

## Public firmware layout

- `firmware/dumps/mt8163/` — original standalone firmware images, configuration dumps and reference kernel module.
- `firmware/extracted/` — canonical extracted system/vendor filesystem tree and HEQ reference payload.
- `firmware/manifests/` — source provenance and SHA-256 inventory for each extracted file.

The split ZIP archives are intentionally not retained after the full extracted tree has been copied and verified against the private source manifest. The extracted tree and its SHA-256 manifest are the durable public firmware dataset.

## Compute workflow contract

- Firmware-only processing runs in `public_calculations`.
- Analysis workflows check out the private repository read-only for code and project helpers, and use firmware from the public repository.
- Workflow logs and artifacts are public. They must not contain credentials, personal data, or private signing material.
- Large intermediate outputs belong in Actions artifacts, not in either Git repository.
- Do not commit generated extracted data or reports to the private project repository.
- Keep project documentation and reviewed findings in the private repository.

## Migration and cleanup

The one-time firmware synchronization imports the full 1,386-entry extracted tree from the historical private location into `public_calculations/firmware/extracted`, verifies each file's size and SHA-256, and writes a public provenance manifest. Only after the public copy is verified may the private `reference-materials/` tree and redundant ZIP archives be deleted.
