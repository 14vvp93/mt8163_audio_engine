# Repository migration readiness

## Repository roles

The project uses two repositories with separate responsibilities:

| Repository | Role | Visibility |
|---|---|---|
| `14vvp93/mt8163_audio_engine` | Canonical product repository: authoritative source code, tests, architecture, product documentation, reviewed decisions and links to validated evidence | Private |
| `14vvp93/public_calculations` | Public firmware/data repository and heavy-compute worker: original dumps, archives, extracted data, analysis tools, resource-intensive Actions and public reports | Public |

The worker's current bootstrap and import gate are tracked in the [Phase 1 status report](https://github.com/14vvp93/public_calculations/blob/main/docs/PHASE1_STATUS.md) and its [workflow migration map](https://github.com/14vvp93/public_calculations/blob/main/docs/WORKFLOW_MIGRATION_MAP.md).

The current working repository, `14march93/mt8163_audio_engine`, is the migration source. The canonical destination is now identified as `14vvp93/mt8163_audio_engine`. `public_calculations` is not a second product repository: it is the approved public home for firmware inputs, derived data, analysis tooling and compute workflows; canonical product source code remains in the private product repository.

## Compute-repository data policy

The owner explicitly approves public storage of firmware dumps, original firmware
archives, extracted firmware contents, technical analysis and generated reports in
`14vvp93/public_calculations`. Firmware dumps are not confidential by default.

Because the compute repository is public, assume its source, commit history, workflow
run metadata, logs, caches and published artifacts may be visible to anyone.

- Do not publish actual credentials, access tokens, passwords, private signing keys,
  personal data or unrelated private material.
- Do not print secrets, environment variables or credential-bearing command lines.
- Validate workflow task names, refs and paths; reject path traversal and unsafe inputs.
- Prefer manual dispatch for expensive work. Do not expose write credentials to
  untrusted pull-request code.
- Keep the canonical product implementation and authoritative product decisions in
  the private product repository. The public worker stores public inputs, tools,
  workflows, derived data and public analysis results.
- Record source archive hashes, member hashes, tool versions and reproducible steps.
- Use Git LFS or GitHub Releases for durable large files after checking current limits
  and quotas. Actions artifacts are temporary, retention-limited outputs.

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
3. Product source code, tests, product scripts and documentation. Public firmware archives and their manifests are maintained in `public_calculations` and referenced from the product repository.
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
- Preserve original public archives and verified manifests in the public compute repository; keep derived extracted trees reproducible and store large outputs using an appropriate durable backend.
- Do not rely on the old repository's time-limited Actions artifacts as the only copy of extracted data.
- The 161 MiB HEQ APK cannot be committed as a normal GitHub file. Its split source volumes belong in `public_calculations`; preserve the volumes and record their hashes. If a permanent assembled copy is needed, choose a storage method supported by the repository and document its checksum.
- Public firmware volumes and assembled firmware may be stored in the public compute repository. Do not place credentials, private signing material, personal data or unrelated private content there.
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
9. Configure `public_calculations` as the public compute worker for approved public firmware inputs, with strict secret hygiene and validated paths.
10. Run critical product and compute workflows and verify generated commits target the correct repository and branch.
11. Compare source and destination refs, tags, tree hashes, archive manifests and workflow results.
12. Only after successful validation, designate `14vvp93/mt8163_audio_engine` as canonical. Keep the old source repository read-only for a defined rollback period.

## Current repository-specific portability findings

- The archive extraction workflow previously pinned checkout, rebase and push operations to `feature/cd3313-driver-rewrite`. It has now been changed to use the triggering Git ref, so it can operate on the branch selected by push or manual dispatch.
- The archive workflow concurrency group is scoped by `github.ref`.
- The firmware configuration audit was previously reported to use the triggering ref and have no hard-coded repository name; this still needs a fresh file-level verification before cutover.
- Historical Actions run URLs in audit documents are evidence only and should continue pointing to the source repository after migration.
- The source repository is private. The canonical destination is also private; the compute repository is public and is approved for firmware dumps and derived public analysis. Keep actual secrets and personal data out.
- The full extraction artifact is temporary and must not be treated as a migration source. Use retained public archive volumes in `public_calculations` plus scripts and manifests to reproduce it.

## Cutover gate

Migration is not complete until:
- source and destination branch/tag inventories match;
- required Git history is present;
- all tracked source archive volumes and curated files match their recorded hashes;
- the destination can reproduce archive inventory and curated extraction;
- public compute workflows do not expose credentials or unrelated private data;
- required tests and analysis workflows pass;
- no workflow writes back to the old repository unintentionally;
- the destination's access controls and integrations are confirmed.
