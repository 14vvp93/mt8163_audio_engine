# Repository boundaries and public compute architecture

## Decision

The MT8163 Audio Engine uses two repositories with deliberately separate responsibilities:

| Repository | Visibility | Role | Canonical contents |
|---|---|---|---|
| [`14vvp93/mt8163_audio_engine`](https://github.com/14vvp93/mt8163_audio_engine) | Private | Product source of truth | Product implementation, build definitions, tests, engineering documentation, reviewed decisions, private CI entry points |
| [`14vvp93/public_calculations`](https://github.com/14vvp93/public_calculations) | Public | Firmware-data store and compute-worker repository | Approved MT8163 firmware inputs, extracted firmware dataset, SHA-256 manifests, reusable GitHub Actions workflows, analysis scripts and public compute infrastructure |

This separation is intentional and must be preserved.

## Private repository: product implementation

The private repository owns the implementation and the engineering decisions for the complete MT8163 / Android 9 audio stack, including:

- Android 9-compatible Primary Audio HAL;
- AudioCore, stream/session lifecycle, routing and mute coordination;
- PCM backend and DSP implementation;
- CD3313EO replacement driver and its dependent integration;
- product build definitions, tests and engineering documentation.

The private repository is the source of truth for product code. Product implementation and private engineering documentation must not be copied into the public compute repository.

## Public repository: firmware inputs and compute

The public repository owns only approved public research inputs and compute infrastructure:

- firmware dumps and extracted firmware data under `firmware/`;
- SHA-256 manifests and integrity inventories;
- reusable GitHub Actions workflows;
- firmware-analysis scripts and bounded compute workers;
- temporary execution results published as workflow artifacts.

It is not a second product-source repository. Do not add product implementation, private engineering documentation, credentials, tokens, private keys, personal data or private signing material.

## Workflow and data flow

The private repository is the operator-facing entry point for public compute:

1. A maintainer manually dispatches `.github/workflows/run-public-compute.yml` or `.github/workflows/run-firmware-analysis.yml` in the private repository.
2. The selected job calls a reusable workflow from `14vvp93/public_calculations` using `workflow_call` and a branch/ref pinned in the caller workflow.
3. The public worker checks out its own public firmware dataset and compute scripts.
4. The worker processes only the selected bounded task and uploads results as Actions artifacts to the calling workflow run.
5. Reviewed findings that become engineering decisions are written back to private project documentation through the normal review process.

The currently inspected public compute workflow checks out `14vvp93/public_calculations` itself. It does not check out the private product repository. Do not assume private source code is available to public workers unless a future, separately reviewed workflow explicitly establishes a secure read-only handoff.

## Security and integrity rules

- Keep product source code and engineering documentation private.
- Treat all firmware inputs as research evidence, not as an authoritative specification or proof of correct behavior.
- Verify firmware data using the committed SHA-256 manifests before analysis.
- Keep workflow permissions read-only unless a narrowly scoped write is explicitly required and reviewed.
- Pin third-party Actions to reviewed full commit SHAs.
- Do not place credentials or private material in public workflow inputs, logs, artifacts or commits.
- Do not flash firmware, install modules, or perform hardware writes from public compute workflows.
- Public workers must not push generated output into either repository. Use workflow artifacts for transient results; promote only reviewed conclusions into private documentation.
- Do not move or duplicate product code into the public repository to work around CI runner or permissions problems.

## Branch and change policy

- Keep ongoing product development on the designated feature branch.
- Do not modify either repository's `main` branch, merge, rebase, reset or force-push without explicit authorization.
- Changes to reusable public workflows must be reviewed as infrastructure changes; callers must use an intentional, reviewed ref.
- A public workflow run is not evidence that product code was built or tested unless that workflow explicitly checks out and tests the relevant private revision.

## Documentation ownership

- The private repository owns this architecture decision and all project-specific operating procedures.
- The public repository README describes its public-data and compute-worker scope.
- If the public README, workflow YAML and private operating contract disagree, inspect the actual workflow definitions and resolve the documentation drift through a reviewed change. Do not infer private checkout, artifact transfer or permissions from prose alone.

## Verified baseline

This document was added on 2026-11-13 after inspecting:

- private branch `feature` at `d52c96f475dfd38075019d1687807ce9f9f1a27e`;
- `.github/workflows/run-public-compute.yml`;
- `.github/workflows/run-firmware-analysis.yml`;
- public `main` workflow `.github/workflows/compute.yml`;
- public repository `README.md`.

The private caller workflows currently reference reusable public workflows at `@main`. Treat that as the observed baseline, not a permanent recommendation: moving to immutable commit-SHA pins requires a deliberate workflow update and compatibility review.
