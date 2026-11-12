# Importing approved firmware from the primary repository

The public compute repository hosts approved public MT8163 firmware archives,
dumps, extracted data and generated analysis reports. The canonical source repository
is `14vvp93/mt8163_audio_engine`; no future import or workflow should depend on
the retired `14march93` account.

## One-time credential setup

Create a fine-grained GitHub personal access token restricted to
`14vvp93/mt8163_audio_engine` with **Contents: Read-only** permission. Add it to
`14vvp93/public_calculations` as an Actions repository secret named
`SOURCE_REPO_READ_TOKEN`. Do not paste the token into chat, workflow inputs, files,
or logs. The import workflow runs only on the `main` branch.

If the existing secret was created for the former `14march93` repository, replace
it with a token that grants read-only access to the current private repository. Secret
values cannot be inspected by workflow code; a missing or insufficient token causes
the import validation step to fail.

## Run the import

Open **Actions → Import approved public MT8163 firmware → Run workflow**.
Select the source branch (default `feature/cd3313-driver-rewrite`) and whether to
include standalone raw dumps. The workflow imports:

- split ZIP volumes from `reference-materials/firmware-dumps/`;
- standalone raw images and configuration dumps when enabled;
- split HEQ archive volumes from `reference-materials/heq/`;
- the original `cd3313.ko` reference module.

The importer hashes each source file before copying, verifies the destination copy
against the source SHA-256, rejects symlinked inputs and destination paths, and
enforces a 95 MiB per-file safety ceiling. It records the exact source commit, source
path, destination path, file size and SHA-256 in
`firmware/manifests/mt8163-source-import.json`, then commits the public inputs to
`main`. It never writes to the primary source repository.

## Size and integrity

The destination is public: imported firmware, extracted content, workflow logs and
artifacts must contain no credentials, tokens, passwords, private signing keys,
personal data, or unrelated private material. The import preserves approved input
bytes; it does not recompress them or silently switch to Git LFS.

Ordinary Git rejects individual blobs above 100 MiB. The workflow uses a conservative
95 MiB ceiling. If a future file exceeds it, stop and select Git LFS, a GitHub Release,
or another suitable large-file backend before importing it.
