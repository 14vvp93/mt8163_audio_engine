# Repository migration readiness

## Objective

Prepare this project to move to a different GitHub repository without changing the product architecture, losing source history, dropping firmware evidence, or leaving CI workflows tied to the current repository name or branch.

The destination repository has not yet been specified. Do not perform a transfer, rename, archive, or destructive cleanup until its exact owner/repository and migration method are confirmed.

## Portability rules

- Do not hard-code the current repository name in scripts or workflow logic unless a URL is explicitly documenting historical evidence.
- Do not hard-code a development branch in reusable workflows. Use the triggering ref (for example `${{ github.ref }}`) or a documented workflow input.
- Use relative links for documentation within this repository.
- Use repository-local paths for scripts and test fixtures.
- Do not assume repository secrets, variables, environments, branch protection, GitHub App access, or Actions permissions will exist in the destination.
- Keep source archives and generated reference manifests self-contained; do not depend on an artifact ID from the old repository.
- Record external URLs and run IDs as historical evidence, but never make future automation depend on them.
- Avoid absolute local paths and personal workstation paths in committed files.

## What must be transferred

1. All Git branches and tags required for project history.
2. The complete Git history, not only the current working-tree snapshot.
3. Source code, tests, scripts, documentation and immutable firmware archive parts.
4. Curated extracted references and their SHA-256 manifest.
5. Git LFS objects, if LFS is introduced before migration.
6. GitHub Actions workflow files and any reusable workflow definitions.
7. Release assets or other large assets that are not stored as normal Git blobs.
8. Issue/PR context or decisions that are not already recorded in project documentation, if the user wants those migrated.

## GitHub settings that do not move with Git history

Before the cutover, inventory and recreate as required:

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

Never copy secret values into Git or documentation. Recreate them through GitHub settings.

## Large firmware inputs

- Preserve original multipart ZIP volumes as immutable source inputs.
- Preserve the curated extracted tree and manifest in Git when file-size limits permit.
- Do not rely on the old repository's time-limited Actions artifacts as the only copy of extracted data.
- The 161 MiB HEQ APK cannot be committed as a normal GitHub file. Its split source volumes remain the canonical tracked input. If a permanent assembled copy is needed, choose a storage method supported by the destination and document its checksum.
- Before migration, verify every split archive volume, archive CRC, extracted member count and SHA-256 manifest from a clean checkout.

## Suggested migration sequence

1. Freeze structural cleanup and record the source repository HEAD.
2. Verify a clean clone can run archive inventory, curation and host tests.
3. Audit Git object sizes and identify any files above GitHub's 100 MiB normal-blob limit.
4. Decide whether any large files should use Git LFS or release assets; do not introduce LFS casually because LFS storage/bandwidth quotas and object transfer must be planned.
5. Create the destination repository with the intended visibility and access.
6. Transfer all branches, tags and full Git history using a mirror-based migration.
7. Transfer required LFS objects and release assets separately, if applicable.
8. Recreate secrets, variables, permissions, branch rules, Apps and integrations.
9. Run all critical workflows on the destination and verify generated commits target the correct branch.
10. Compare source and destination refs, tags, tree hashes, archive manifests and workflow results.
11. Only after successful validation, designate the destination as canonical. Keep the source repository read-only for a defined rollback period.

## Current repository-specific portability findings

- The archive extraction workflow previously pinned checkout, rebase and push operations to `feature/cd3313-driver-rewrite`. It has now been changed to use the triggering Git ref, so it can operate on the branch selected by push or manual dispatch.
- The archive workflow concurrency group is now scoped by `github.ref`, preventing unrelated branches from cancelling or serializing each other's work unnecessarily.
- The firmware configuration audit uses the default triggering ref and has no hard-coded repository name.
- Historical Actions run URLs in audit documents are evidence only and are expected to continue pointing to the source repository after migration.
- The current repository is private. Destination visibility and GitHub App access must be explicitly recreated.
- The full extraction artifact is temporary and must not be treated as a migration source. Use the retained archive volumes plus scripts and manifests to reproduce it.

## Cutover gate

Migration is not complete until:
- source and destination branch/tag inventories match;
- required Git history is present;
- all tracked source archive volumes and curated files match their recorded hashes;
- the destination can reproduce archive inventory and curated extraction;
- required tests and analysis workflows pass on the destination;
- no workflow writes back to the old repository unintentionally;
- the destination's access controls and integrations are confirmed.
