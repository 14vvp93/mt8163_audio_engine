# Repository layout

- .github/workflows/ — reusable firmware analysis and compute workers.
- firmware/dumps/mt8163/ — 22 immutable standalone firmware dumps.
- firmware/extracted/ — 1,386 SHA-256-verified extracted firmware files.
- firmware/manifests/ — extracted-file, raw-dump and HEQ integrity manifests.
- scripts/firmware/ — inventory, safe extraction and analysis tools.
- tests/ — offline tests for compute tooling.

## Storage boundaries

- Keep firmware dumps, extracted firmware and machine-readable manifests under firmware/.
- The extracted tree is the canonical durable dataset; original split ZIP volumes were removed after complete verification.
- Do not store product implementation code or project documentation here.
- Do not store generated analysis reports in Git; upload them as Actions artifacts.
- Keep all product code, build definitions, tests and engineering documentation in the private 14vvp93/mt8163_audio_engine repository.
- Keep the root README concise and focused on the public data/compute repository's purpose.
