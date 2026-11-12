# Repository migration and final ownership

## Current repositories

| Repository | Responsibility | Visibility |
|---|---|---|
| `14vvp93/mt8163_audio_engine` | Canonical MT8163 Audio Engine product source, builds, tests, engineering documentation and reviewed findings | Private |
| `14vvp93/public_calculations` | Approved firmware dataset, SHA-256 manifests, heavy firmware analysis scripts and reusable GitHub Actions workers | Public |

The former `14march93/mt8163_audio_engine` repository is a historical migration source. The canonical project is now `14vvp93/mt8163_audio_engine`.

## Final data boundary

The private repository contains product code and documentation only. Its current main tree has no firmware archives, extracted firmware, firmware dumps or firmware binaries.

The public repository contains:
- 1,386 extracted firmware files (578,175,653 bytes);
- 22 standalone raw dumps (44,050,240 bytes);
- three machine-readable manifests;
- reusable heavy-analysis workflows, compute scripts and tests.

The original 26 split ZIP volumes (349,781,724 bytes) were removed from current branch trees after the full extracted tree was copied to the public repository and every file's size and SHA-256 was verified. The HEQ APK remains as three verified chunks, not as ZIP archives.

See [firmware dataset migration](public-compute/FIRMWARE_DATASET_LAYOUT.md) and the [final cleanup report](repository-audit/REPOSITORY_CLEANUP_REPORT.md) for the detailed inventory.

## Cross-repository execution

The private project provides two manual operator entry points:

- `run-public-compute.yml` calls public `compute.yml` for inventory, integrity verification and audio-stack inventory.
- `run-firmware-analysis.yml` calls one of nine specialized public firmware workers.

Public workers read firmware from their own repository checkout. They do not check out private source code and do not require a private repository token. Results are uploaded as GitHub Actions artifacts to the caller's run.

The nine workers all passed one end-to-end validation run. The separate compute tasks `verify` and `audio-inventory` also passed. Current workflow and repository-integrity results are recorded in the final cleanup report.

## Branch cleanup

The current trees of all four private branches were cleaned of firmware data and old duplicate firmware-analysis workflows:

- `main`
- `feature/cd3313-driver-rewrite`
- `archive/14march93-main`
- `ci/validate-sound-profile-fixes`

The CI branch's unique source-code change was preserved while its firmware archives and stale analysis workflows were removed.

## Security and permissions

- Public workers need only read access to their own public repository.
- No current workflow references `SOURCE_REPO_READ_TOKEN`.
- The former read token is now unused and should be removed manually from Actions secrets in both repositories.
- Public logs and artifacts must never contain credentials, private signing material or personal data.
- Public analysis workers do not install firmware or write to a physical MT8163 device.

## Git history and storage

The cleanup changed current branch trees but did not rewrite Git history. Deleted firmware blobs remain in historical commits. This preserves the original evidence and commit ancestry, but it means GitHub's stored repository size may not decrease in proportion to the current working-tree cleanup.

A full history rewrite using a filter-repo-style operation would change commit IDs and require force-pushing every affected branch, followed by coordinated recloning. That destructive storage optimization has not been performed and requires separate approval.

## Final state

The requested repository split is in place: private code and documentation; public firmware data and heavy compute. The public extracted dataset is hash-verified, all nine workers passed, both compute verification tasks passed, and private Markdown link integrity passed.
