# Firmware data and storage policy

## Public-data scope

The public repository stores only approved MT8163 firmware data and the tooling required to process it:

- standalone raw firmware images, configuration dumps and reference modules;
- the verified extracted System/Vendor filesystem and HEQ payload;
- SHA-256 manifests and provenance records;
- reusable GitHub Actions workflows, compute scripts and their tests.

Product implementation code, project documentation and reviewed engineering decisions remain in the private `mt8163_audio_engine` repository.

## Archive policy

The initial dataset was imported and fully extracted. The 26 original split ZIP volumes (349,781,724 bytes) were removed from both repositories only after all 1,386 extracted files had been copied and SHA-256 verified in the public repository.

The extracted tree is now the canonical durable firmware dataset. The standalone raw dumps remain immutable. For future firmware updates, upload approved data directly to the public repository, generate a new manifest and run the integrity worker. Do not retain a second archive copy when its complete extracted contents and provenance are already preserved.

## Exclusions

Never commit or print access tokens, passwords, credentials, private signing keys, personal data or unrelated private material. Logs and artifacts are public. Review unexpected files before publishing.

## Integrity

- Record filename/path, byte size, SHA-256 and provenance for every firmware file.
- Preserve original filenames and partition paths.
- Reject path traversal, unsafe archive entries and duplicate extraction destinations when processing new archives.
- Never silently overwrite a different firmware build.
- Do not use Actions artifacts as the only durable copy of approved firmware data.
