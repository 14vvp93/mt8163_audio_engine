# Factory code correctness policy

## Status

This policy applies to every firmware-derived behavior used by the MT8163 Audio Engine.
The factory implementation is evidence about the shipped system, not a normative
specification and not proof that its behavior is correct.

## Mandatory rules

1. **Do not copy factory behavior by default.** Recover the call contract, data
   layout, ownership, state transitions and hardware dependency before adopting it.
2. **Separate compatibility from correctness.** Preserve Android 9 HAL ABI,
   required vendor ABI and externally observable behavior where compatibility
   requires it. Do not preserve an implementation defect merely because the
   factory binary contains it.
3. **Treat suspicious behavior as a hypothesis.** Examples include permissive
   masks, silent format normalization, ignored return values, stale state,
   unchecked arithmetic, inconsistent error handling and route-specific
   fallbacks. Verify each against callers, callees, framework contracts and
   hardware documentation.
4. **Prefer explicit rejection over silent coercion.** Unsupported formats,
   channel layouts, routes and state transitions must fail or bypass according
   to the public contract; do not silently reinterpret them unless the
   compatibility requirement is demonstrated.
5. **Keep evidence and decisions traceable.** For every adopted factory quirk,
   record the binary/source evidence, caller-visible effect, compatibility
   reason, test and rollback/bypass behavior.
6. **Test invariants independently.** Tests must assert the intended contract,
   not merely reproduce the factory implementation. Include malformed inputs,
   boundary values, overflow, concurrent callbacks and backend failures.
7. **Do not infer hardware truth from names or constants.** Endpoint indices,
   channel topology, PCM formats, ioctl framing and register semantics require
   corroborating evidence. Runtime-only facts remain explicitly unverified
   until measured on the target.

## Review procedure for each recovered behavior

- Identify the exact firmware artifact and build identity.
- Trace the complete caller/callee path and determine who owns the state.
- Compare the behavior with Android 9, kernel, ALSA and chip-level contracts.
- Classify it as:
  - **Required compatibility:** externally observable behavior that must remain;
  - **Verified hardware contract:** behavior required by the actual hardware;
  - **Factory implementation detail:** not inherently required;
  - **Suspected defect:** behavior that violates a stronger contract or invariant;
  - **Unresolved:** insufficient evidence to decide.
- Implement the smallest correct abstraction, with explicit validation and
  bounded failure behavior.
- Add tests for both valid behavior and the suspected defect.
- Document any intentional compatibility exception.

## MT8163 examples and constraints

- The Normal Speaker path's stereo-to-mono fold before MTK post-processing is
  a recovered factory behavior. It must be preserved only where required for
  factory route compatibility; it is not evidence that the DSP insertion
  boundary carries independent stereo or four-channel audio.
- Factory stream format/rate/channel acceptance rules must be checked against
  Android 9 channel-mask semantics and the actual policy/profile. Do not copy
  permissive predicates or silent normalization without validating the result.
- ALSA card/device numbers and PCM capabilities must come from target evidence.
  Do not enable guessed endpoints.
- The recovered private callback tail and legacy hardware pointer must not be
  treated as a stable ABI until their exact layout and call semantics are
  independently verified.

## Release gate

No factory-derived behavior becomes a production requirement solely because it
exists in the stock binary. A behavior with unresolved correctness or hardware
semantics stays isolated, disabled, or fail-closed until its contract is
verified. The replacement must be more robust than the factory implementation
without breaking the compatibility contracts that the target actually needs.
