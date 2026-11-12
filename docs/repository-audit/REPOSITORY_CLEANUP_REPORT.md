# Repository cleanup report — 2026-11-12

## Final ownership model

- Private project: `14vvp93/mt8163_audio_engine` — product source code, build definitions, tests, engineering documentation and reviewed findings.
- Public data/compute: `14vvp93/public_calculations` — approved firmware files, extracted firmware, SHA-256 manifests, heavy-analysis scripts, tests and reusable GitHub Actions workers.

The public repository no longer contains project documentation, reports or product implementation code. The private repository no longer contains firmware dumps, extracted firmware or archive volumes in its current branch trees.

## Current tree inventory

| Repository | Current tracked files | Firmware files | Status |
|---|---:|---:|---|
| Private `mt8163_audio_engine` main | 145 | 0 | Code and documentation only |
| Public `public_calculations` main | 1,431 | 1,411 | Firmware data and compute infrastructure |

Public firmware inventory:

| Dataset | Files | Bytes |
|---|---:|---:|
| Extracted firmware | 1,386 | 578,175,653 |
| Standalone raw dumps | 22 | 44,050,240 |
| Machine-readable manifests | 3 | — |
| Original split ZIP volumes removed | 26 | 349,781,724 |

The extracted tree includes the System/Vendor filesystem and three HEQ APK chunks. The HEQ chunks are not ZIP archives; they are retained because they reconstruct the original 168,811,733-byte APK and each chunk is individually hash-verified.

## Verified transfer and deletion

The one-time import workflow copied all 1,386 extracted firmware files from the former private location into the public repository. It verified each destination file's size and SHA-256 against the private source manifest before committing.

- Import run: [43236544152](https://github.com/14vvp93/public_calculations/actions/runs/43236544152) — success.
- Source commit: `44e132c8ac299ebfd58831232ec6b6f64ae8f769`.
- Extracted manifest: `firmware/manifests/mt8163-extracted.json`.
- Raw dump provenance: `firmware/manifests/mt8163-source-import.json`.
- HEQ manifest: `firmware/manifests/heq-apk.json`.

After verification, the 26 original split ZIP volumes were removed from the current working trees. The complete private `reference-materials/` directory was removed from the current private main branch. The obsolete private firmware-analysis workflows and archive-processing project scripts were also removed. Documentation and reports formerly stored in the public repository were copied into the private documentation tree before public cleanup.

## Workflow migration

All nine firmware workers now read only from the public firmware dataset. They no longer check out the private project or require a private read token.

End-to-end worker validation: [43238133570](https://github.com/14vvp93/public_calculations/actions/runs/43238133570) — all nine jobs passed.

Compute validation: [43239612147](https://github.com/14vvp93/public_calculations/actions/runs/43239612147) — both `verify` and `audio-inventory` passed.

Public infrastructure validation: [43239765157](https://github.com/14vvp93/public_calculations/actions/runs/43239765157) — passed.

Private documentation/link integrity: [43240367918](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43240367918) — passed.

## Branch cleanup

The current trees of all four private branches were checked and cleaned:

- `main`
- `feature/cd3313-driver-rewrite`
- `archive/14march93-main`
- `ci/validate-sound-profile-fixes`

No current branch tree contains `reference-materials/`, firmware archive volumes, or the old standalone firmware binaries. The unique CI-branch source change was preserved; its firmware files and duplicate firmware-analysis workflows were removed.

## Remaining caveats

1. **Git history was not rewritten.** Deleted archives and firmware blobs remain in historical commits. The current working trees are clean, but GitHub's stored repository size may not shrink materially until an approved history rewrite and force-push are performed. That operation is intentionally not done because it changes commit IDs and affects every clone/fork.
2. The unused Actions secret `SOURCE_REPO_READ_TOKEN` is no longer referenced by either repository's current workflows. Remove it manually from Actions secrets in both repositories; the available GitHub connection does not expose secret deletion.
3. The earlier Android test APK build dependency-resolution failure is separate from repository cleanup and remains a build-system follow-up.

## Final decision

The current branch contents now follow the requested separation: private code and documentation; public firmware and heavy compute. The public extracted dataset and all nine workers have been verified. No firmware-only files remain in the current private branch trees, and no original ZIP archives remain in the current public main tree.
