# CI Execution Diagnostics — 2026-11-13

**Repository:** `14vvp93/mt8163_audio_engine`  
**Branch:** `feature`  
**Commit:** `9c67cc41db1c69b7ecdd27fd7f9ba672f72c5427`  
**Purpose:** Record the latest CI evidence without attributing infrastructure failures to source code.

## Latest workflow attempts

| Workflow | Run ID | Job | Job ID | Conclusion |
|---|---:|---|---:|---|
| Repository integrity | 43436764155 | markdown-links | 131923776581 | failure |
| PCM Stream Broker ThreadSanitizer | 43436764273 | pcm-broker-tsan | 131923776985 | failure |
| AudioCore ThreadSanitizer | 43436764399 | audio-core-tsan | 131923777277 | failure |

All three runs target the same feature commit.

## Diagnostic retrieval

- GitHub's workflow-run jobs endpoint returned the three job records and their failed conclusions.
- The workflow-job-steps endpoint returned an empty `steps` array for all three jobs.
- The workflow-job-logs endpoint failed for all three jobs with storage error `BlobNotFound`.
- No compiler output, CMake output, CTest output, Python traceback, shell diagnostic or TSAN report was available.
- The available job summaries do not expose a runner assignment field, so this note does not assert a specific runner ID or runner-allocation failure.

## Engineering conclusion

The GitHub Actions runs are marked failed, but the available evidence does **not** establish that any repository command executed or that any source-code test failed. Treat these runs as **CI execution/diagnostic unavailable**, not as a confirmed code regression and not as a pass.

Do not change source, tests or workflow commands based only on these conclusions. Obtain a run with populated job steps/logs, or reproduce the exact workflow commands in a controlled environment, before diagnosing a code-level failure.

## Branch safety

- `main` was not modified.
- No merge, rebase, reset, force-push, workflow rerun or hardware operation was performed as part of this diagnostic.
- Hardware transport remains disabled; no device writes are authorized by this note.

## Next actions

1. Retry or dispatch the affected workflows only when GitHub can provide executable job steps and logs.
2. If a job again has no steps, capture the complete job metadata from GitHub's UI/API and investigate Actions runner/service availability.
3. Once a step executes, fix only the specific reproducible command/test failure.
4. Keep the Primary HAL marked non-installable until the complete factory contract and target-device gates pass.
