# Firmware data and storage policy

## Public-data approval

The repository owner explicitly approves public storage of firmware dumps, original
firmware archives, extracted firmware contents, technical analysis, and generated
reports. These materials are not treated as confidential by default.

## Exclusions

Never commit or print actual access tokens, passwords, credentials, private signing
keys, personal data, or unrelated private material. Review unexpected files before
publishing. GitHub Actions logs, caches, artifacts, and this repository's history
must be treated as public.

## Preserve source material

- Preserve each original archive byte-for-byte.
- Record filename, byte size, SHA-256, source/provenance, and upload date where known.
- Do not delete split archive volumes or rename them without recording the mapping.
- Do not replace a source archive with an extracted or recompressed copy.
- Keep extraction output separate from original inputs.

## GitHub size strategy

Use ordinary Git for source code, small manifests, and compact reports. For large
archives or large extracted outputs, choose Git LFS or GitHub Releases after checking
current account/repository quotas and transfer constraints. Actions artifacts are
temporary and retention-limited; they are not the sole durable copy of important data.
Do not assume a particular quota or retention allowance without checking current settings.

## Integrity

All archive processing must verify the input SHA-256 and produce per-member hashes.
Extraction must reject absolute paths and parent-directory traversal. Preserve duplicate
member names in the inventory and report collisions rather than silently overwriting.
