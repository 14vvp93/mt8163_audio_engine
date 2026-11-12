# Compute worker contract

## Purpose

This repository is the public, resource-intensive worker for
`14vvp93/mt8163_audio_engine`. It owns approved public firmware inputs, extraction
and analysis tooling, and generated compute reports. It is not a second product source
repository.

## Invocation

The worker exposes `.github/workflows/compute.yml` in two modes:

- **Reusable workflow:** called by
  `14vvp93/mt8163_audio_engine/.github/workflows/run-public-compute.yml`.
  The product repository is the operator entry point; the worker checks out its own
  public scripts and data and runs the selected task.
- **Direct workflow dispatch:** available for worker maintenance and standalone
  compute operations.
- **Approval-marker push:** the existing `firmware/EXTRACT_APPROVED` and
  `firmware/AUDIO_ANALYSIS_APPROVED` markers can start the bounded worker pipeline.

Supported tasks:

- `inventory`: hash and inventory supported firmware archives.
- `extract`: numeric reassembly of multipart ZIP volumes, CRC validation,
  path/symlink/duplicate checks, extraction into a temporary workspace, and manifest
  generation.
- `audio-inventory`: extraction plus Android audio-stack and DSP inventory.
- `all-light`: archive inventory and lightweight checks without committing output.

The worker always checks out `14vvp93/public_calculations@main` into a dedicated
`worker/` directory. Its scripts and data are therefore independent of the caller's
checkout. Results are uploaded as artifacts to the calling workflow run when invoked
reusably, or to the worker run when dispatched directly.

## Inputs and outputs

- Inputs are approved public archives under `worker/firmware/archives/`.
- Raw input bytes are immutable. The worker computes SHA-256 before processing.
- Outputs include JSON manifests, text reports, and Actions artifacts.
- Every report should record source hashes, tool versions, task name, and completion
  status.
- Partial or failed extraction must be marked incomplete and must not be presented as
  a complete firmware inventory.
- Artifacts are temporary; reviewed compact findings may be promoted into the product
  repository.

## Security

This repository is public. Firmware dumps and extracted firmware contents are approved
for public storage, but credentials, tokens, passwords, private signing keys, personal
data, and unrelated private material are not. Logs and artifacts are public too.
Never echo secrets.

The reusable compute workflow needs no cross-repository token: it reads public worker
data and uses read-only permissions. Firmware import is a separate operation and uses
`SOURCE_REPO_READ_TOKEN` with read-only Contents access to the private primary
repository. Keep that token in Actions secrets only.

## Reliability

- Pin third-party Actions to reviewed full commit SHAs before production use.
- Pin Python and analysis-tool versions.
- Use explicit timeouts and concurrency cancellation for duplicate runs.
- Upload intermediate artifacts with a documented retention period.
- Do not silently push generated files to the product repository.
- Promotion into the canonical repository requires review and a source commit/hash.
