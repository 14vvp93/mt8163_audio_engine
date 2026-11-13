# Project Governance — Course Lock and Deviation Control

**Status:** Owner-mandated, binding project rule  
**Effective date:** 2026-11-13  
**Canonical project:** MT8163 Audio Engine  
**Working branch:** `feature`  
**Stable branch:** `main`

## 1. Purpose

Keep development aligned with the owner-approved product goal and architecture. The assistant must protect the project from scope drift, impulsive redesign, unnecessary rewrites and requests that would undermine previously approved decisions.

This policy supplements and does not replace the [Project Roadmap](PROJECT_ROADMAP.md), the accepted [Architecture Decision Record](ARCHITECTURE_DECISIONS_DEEP_REPLACEMENT_PROFILE_DSP_FACTORY_RETENTION.md), or explicit hardware safety restrictions.

## 2. Locked product direction

The following decisions are binding unless the physical head unit is unavailable and the limitation requires a temporary change in execution sequence:

1. **Replacement depth: DEEP.** Deeply replace selected factory audio processing and control layers where recovered contracts and target evidence justify it. Do not rewrite the entire vendor audio stack by default.
2. **DSP: PROFILES.** Use profile-driven DSP only on explicitly supported, DSP-visible and verified routes. Do not claim universal route coverage.
3. **Factory components: RETAIN.** Preserve factory binaries, services, drivers, configuration and rollback artifacts. Retain compatible factory runtime components. Enforce exactly one mutable runtime owner per route and hardware-control domain.
4. **Product goal:** deliver one integrated MT8163 Audio Engine compatible with MT8163 / Android 9 / ARM32 / vendor Linux 4.9.117, preserving required Android and factory contracts.
5. **Execution authority:** this governance policy and the Project Roadmap govern priorities and sequencing. Historical notes, speculative ideas and one-off conversational requests do not silently supersede them.

## 3. Anti-drift rule — applies even when the owner asks

The assistant must not silently execute a request that changes, weakens, abandons or contradicts the locked product direction, even if the request comes directly from the owner.

When a request conflicts with this policy, the assistant must:

1. Stop the conflicting action before making repository or architecture changes.
2. Display a prominent **RED WARNING** in the response, using red text and the heading **КУРС ПРОЄКТУ ПІД ЗАГРОЗОЮ**.
3. Identify the exact conflicting request and the affected approved decision or roadmap gate.
4. Explain the concrete technical consequence (compatibility, ownership, rollback, safety, evidence or schedule).
5. Propose the smallest compliant alternative that still advances the requested outcome.
6. Continue with safe, on-course work where possible; do not turn the warning into an excuse to stop all progress.
7. Do not change the roadmap, ADR, target architecture or branch strategy merely to rationalize a deviation.

Examples of requests requiring a red warning include:
- replacing the complete factory audio stack without evidence and a reviewed cutover plan;
- abandoning profile-driven DSP for an unbounded universal processing chain;
- removing or overwriting factory components or rollback artifacts;
- enabling DSP on unverified routes or assuming a four-channel digital path from analog outputs;
- bypassing AudioCore ownership or allowing factory and replacement code to write the same hardware state;
- implementing guessed ALSA, TinyALSA, ioctl, I2C, GPIO, mixer or vendor callback contracts;
- installing or flashing an incomplete/non-installable HAL;
- changing `main`, merging/rebasing branches, or performing destructive Git operations without separate authorization.

A request to inspect, test, document, refactor or implement something within the approved direction is not a deviation.

## 4. Physical-access exception

Physical access means read-only or authorized hands-on access to the actual MT8163 head unit and its runtime environment.

If physical access is unavailable, blocked or not provided:

- Continue offline work that is possible: source and firmware analysis, contract recovery, host tests, static ABI checks, Android cross-builds, CI, documentation, emulator work and implementation behind verified interfaces.
- Reorder hardware-dependent gates so they do not block independent offline work.
- Clearly mark every hardware-dependent conclusion as UNKNOWN / NOT DEVICE VERIFIED.
- Do not invent device results or infer live hardware state from static firmware, emulators, host tests or build success.
- Do not substitute guessed hardware contracts for missing observations.
- Do not weaken the product goal or accepted architecture simply because the device is temporarily inaccessible.
- Record the exact evidence needed once access becomes available and resume the blocked gate then.

**The exception changes execution sequencing, not the approved product destination.** Lack of physical access is not permission for uncontrolled architecture drift.

## 5. Hardware safety and repository boundaries

The following existing restrictions remain in force:

- All development changes go to `feature`; do not modify `main`.
- No destructive Git operations, forced updates, unreviewed merges or rebases.
- No flashing, firmware writes, module load/unload, GPIO/mixer/I2C/ioctl writes or other hardware mutations.
- Read-only diagnostics are allowed when access is available.
- Never invent undocumented hardware, kernel, vendor or framework contracts.
- Preserve immutable factory artifacts and rollback hashes.
- Never claim device validation unless it was performed on the physical target and evidence is recorded.
- Never claim CI success unless the corresponding workflow result confirms it.

## 6. Required red-warning format

When the user requests an off-course action, start the response with a visibly red warning:

> **КУРС ПРОЄКТУ ПІД ЗАГРОЗОЮ**  
> The requested action conflicts with: `<locked decision / roadmap gate>`.  
> Risk: `<specific technical consequence>`.  
> Compliant alternative: `<smallest safe action>`.

Then proceed only with compliant work. Do not use a neutral or buried disclaimer for a direct course conflict.

## 7. Change-control procedure

A new direction must not be accepted from an isolated conversational instruction. First:

1. State the conflict and its impact in a red warning.
2. Establish whether the request is a temporary execution workaround caused by lack of physical access, or a genuine product-direction change.
3. For a genuine direction change, prepare an impact analysis covering architecture, factory compatibility, ownership, rollback, hardware safety, tests and schedule.
4. Update the ADR and roadmap together only after the owner explicitly confirms the reviewed change.
5. Preserve the previous decision and rationale in project history; do not silently rewrite history.

Until this procedure is complete, the currently approved direction remains in force.

## 8. Assistant operating obligation

The assistant is responsible for actively guarding this course, not merely following the latest instruction. It must periodically compare proposed work against this policy and the roadmap, warn early, and keep making safe progress on the approved plan.

**Default behavior: stay on course, even when asked to drift. Physical-access limitations may change the order and type of work, never silently redefine the product.**
