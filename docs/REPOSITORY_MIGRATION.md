# Repository migration readiness

## Repository roles

The project uses two repositories with separate responsibilities:

| Repository | Role | Visibility |
|---|---|---|
| `14vvp93/mt8163_audio_engine` | Canonical product repository: source code, tests, documentation, firmware evidence, manifests and validated results | Private |
| `14vvp93/public_calculations` | Heavy-compute worker: resource-intensive GitHub Actions for archive processing, ELF analysis, decompilation, large test matrices and builds | Public |

The current working repository, `14march93/mt8163_audio_engine`, is the migration source. The canonical destination is now identified as `14vvp93/mt8163_audio_engine`. Do not treat `public_calculations` as a second product repository or as the canonical storage location for firmware/source data.

## Compute-repository safety boundary

Because `public_calculations` is public, assume workflow source, run history, logs and published artifacts may be visible to anyone.

- Never copy private firmware dumps, proprietary binaries, signing material, credentials, private source archives or unredacted extracted trees into the public compute repository or its artifacts.
- Heavy jobs should fetch required inputs from the private canonical repository using a narrowly scoped GitHub App token or fine-grained token stored as a secret. Do not print tokens or sensitive file contents.
- Keep public workflow inputs explicit and validate repository, ref, task name and paths before processing.
- Prefer manual dispatch or trusted workflow calls from protected refs. Do not expose private-repository credentials to untrusted pull-request code.
- Publish only reviewed, sanitized outputs to the public worker repository. Send private or detailed results directly to the private canonical repository, or retain them as private artifacts in the canonical repository.
- Treat Actions logs as public: avoid verbose shell tracing, dumping environment variables, printing extracted data, or emitting sensitive filenames/content.
- Use short-lived, least-privilege credentials and grant only the required contents permissions.
- The compute repository stores reusable scripts/workflows and non-sensitive reports, not the project's authoritative source of truth.

## Portability rules

- Do not hard-code repository names in reusable workflow logic unless a URL is explicitly documenting historical evidence.
- Do not hard-code a development branch in reusable workflows. Use the triggering ref (for example `${{ github.ref }}`) or a documented workflow input.
- Use relative links for documentation within each repository.
- Use repository-local paths for scripts and test fixtures.
- Do not assume repository secrets, variables, environments, branch protection, GitHub App access, or Actions permissions will exist in either repository.
- Keep source archives and generated reference manifests self-contained; do not depend on an artifact ID from the old repository.
- Record external URLs and run IDs as historical evidence, but never make future automation depend on them.
- Avoid absolute local paths and personal workstation paths in committed files.

## What must be transferred to the canonical product repository

1. All Git branches and tags required for project history.
2. The complete Git history, not only the current working-tree snapshot.
3. Source code, tests, scripts, documentation and immutable firmware archive parts.
4. Curated extracted references and their SHA-256 manifest.
5. Git LFS objects, if LFS is introduced before migration.
6. Product workflows and any reusable workflow definitions that belong with the source.
7. Release assets or other large assets that are not stored as normal Git blobs.
8. Issue/PR context or decisions that are not already recorded in project documentation, if the user wants those migrated.

Heavy-compute workflows may live in `public_calculations`, but the product repository must retain a clear index of each job, its inputs, output schema, version and corresponding result location.

## GitHub settings that do not move with Git history

Before cutover, inventory and recreate as required:

- repository visibility and access permissions;
- GitHub App / ChatGPT repository access;
- Actions permissions and allowed actions;
- repository and environment secrets;
- repository variables;
- branch protection and rulesets;
- default branch;
- Actions artifact retention settings;
- releases and release assets;
- webhooks, deploy keys and external integrations.

Never copy secret values into Git or documentation.

## Large firmware inputs

- Preserve original multipart ZIP volumes as immutable source inputs.
- Preserve the curated extracted tree and manifest in the private canonical repository when file-size limits permit.
- Do not rely on the old repository's time-limited Actions artifacts as the only copy of extracted data.
- The 161 MiB HEQ APK cannot be committed as a normal GitHub file. Its split source volumes remain the canonical tracked input. If a permanent assembled copy is needed, choose a storage method supported by the destination and document its checksum.
- Do not place private source volumes or assembled firmware in the public compute repository, its cache, or its artifacts.
- Before migration, verify every split archive volume, archive CRC, extracted member count and SHA-256 manifest from a clean checkout.

## Suggested migration sequence

1. Freeze structural cleanup and record the source repository HEAD.
2. Verify a clean clone can run archive inventory, curation and host tests.
3. Audit Git object sizes and identify any files above GitHub's 100 MiB normal-blob limit.
4. Decide whether any large files should use Git LFS or release assets; do not introduce LFS casually because LFS storage/bandwidth quotas and object transfer must be planned.
5. Confirm the canonical destination repository `14vvp93/mt8163_audio_engine` and its intended private access.
6. Transfer all branches, tags and full Git history using a mirror-based migration.
7. Transfer required LFS objects and release assets separately, if applicable.
8. Recreate secrets, variables, permissions, branch rules, Apps and integrations.
9. Configure `public_calculations` as a compute worker with narrowly scoped access to private inputs and a strict output-sanitization boundary.
10. Run critical product and compute workflows and verify generated commits target the correct repository and branch.
11. Compare source and destination refs, tags, tree hashes, archive manifests and workflow results.
12. Only after successful validation, designate `14vvp93/mt8163_audio_engine` as canonical. Keep the old source repository read-only for a defined rollback period.

## Current repository-specific portability findings

- The archive extraction workflow previously pinned checkout, rebase and push operations to `feature/cd3313-driver-rewrite`. It has now been changed to use the triggering Git ref, so it can operate on the branch selected by push or manual dispatch.
- The archive workflow concurrency group is scoped by `github.ref`.
- The firmware configuration audit was previously reported to use the triggering ref and have no hard-coded repository name; this still needs a fresh file-level verification before cutover.
- Historical Actions run URLs in audit documents are evidence only and should continue pointing to the source repository after migration.
- The source repository is private. The canonical destination is also private; the compute repository is public. These visibility boundaries must be preserved.
- The full extraction artifact is temporary and must not be treated as a migration source. Use retained archive volumes plus scripts and manifests to reproduce it.

## Cutover gate

Migration is not complete until:
- source and destination branch/tag inventories match;
- required Git history is present;
- all tracked source archive volumes and curated files match their recorded hashes;
- the destination can reproduce archive inventory and curated extraction;
- public compute workflows cannot publish private input data;
- required tests and analysis workflows pass;
- no workflow writes back to the old repository unintentionally;
- the destination's access controls and integrations are confirmed.
