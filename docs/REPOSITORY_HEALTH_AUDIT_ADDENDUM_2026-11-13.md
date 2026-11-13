# Repository Audit Addendum — 2026-11-13

This addendum corrects and extends the initial [Repository Health Audit](REPOSITORY_HEALTH_AUDIT_2026-11-13.md). It is based on the live refs and check-run metadata retrieved after that document was committed.

## Corrected branch comparison

Observed refs at time of this addendum:
- `feature`: `c5083fc364151ff05d1410576b182da88786c80d`
- `main`: `0490b0be651e6e1fa8c7b6b8e6f56d9262479a26`

GitHub compare `feature...main` reports:
- `main` has 2 commits not in `feature`.
- `feature` has 592 commits not in `main`.
- The merge base is `d38c020763cb448fe0bceb00c6463cbb7d13c3a7`.
- The compare endpoint reports **zero changed files**.

The two main-only commits are:
1. `4f5944a2c95e904019f2568fb08b0ef1fc8eb96a` — add APS2 relocation audit tool.
2. `0490b0be651e6e1fa8c7b6b8e6f56d9262479a26` — revert that tool from stable main.

The net tree difference is therefore zero in the GitHub compare result. The branches have divergent histories, but their current tracked file trees compare identical. This is not a reason to merge or rewrite either history. Keep development on `feature`; preserve `main` as-is until a deliberate history strategy is approved.

## Corrected current CI state

On the previous code HEAD `84cc9aecd2852e095545c0f7fb36f9a927d0391a`, check-run metadata confirms three failures:
- `pcm-broker-tsan` — check 131896066662.
- `markdown-links` — check 131896066252.
- `audio-core-tsan` — check 131896066210.

After the audit document commit `c5083fc364151ff05d1410576b182da88786c80d`, GitHub started the same three workflows and all three again concluded failure:
- Repository Integrity / `markdown-links` — run 43436477374, job 131922785637.
- AudioCore ThreadSanitizer / `audio-core-tsan` — run 43436477390, job 131922785346.
- PCM Stream Broker ThreadSanitizer / `pcm-broker-tsan` — run 43436477303, job 131922785482.

The GitHub job-log download endpoint returned BlobNotFound for these job IDs, and the check annotation endpoint was not accessible through the current connector. Therefore the precise failing step and root cause remain unknown. Do not change test logic, sparse checkout or sanitizer flags based on speculation. The next fix requires a retrievable job log or the check-run annotation text.

## Status-document inconsistency

`docs/PROJECT_STATUS.md` currently contains older HEAD and CI statements, including a statement that jobs failed before runner assignment and a historical all-green workflow matrix. Those claims do not describe the current live check-run metadata and must not be used as the current status. Preserve them only as dated historical evidence if their original run IDs support them. Update the status document once the exact check failure annotations are available.

## Immediate next actions

1. Retrieve the three check annotations/logs through a working GitHub Actions interface.
2. Identify exact failing step, command and diagnostic.
3. Apply minimal targeted fixes on `feature` only.
4. Re-run the three affected workflows and the full relevant verification matrix.
5. Update `docs/PROJECT_STATUS.md` with the exact tested commit and links to fresh successful runs.
6. Do not merge, reset, force-push or modify `main`.
