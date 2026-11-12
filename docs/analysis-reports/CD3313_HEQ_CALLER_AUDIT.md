# HEQ application caller audit for CD3313

Date: 2026-11-12

## Evidence identity

- Source artifact: `heq-decompiled-analysis`
- Firmware analysis workflow run: [43238133570](https://github.com/14vvp93/public_calculations/actions/runs/43238133570)
- APK SHA-256: `a7c6e1163b56270b506a4b8b6f1ae763a4c4ecc9fae5b0a498aeea46968759b3`
- Decompiled caller: `com.hcn.autoeq.data.AspSettings`
- Related caller: `com.hcn.autoeq.data.AspBalanceHandler`

This is static APK decompilation. It confirms high-level API invocations and argument domains in this HEQ build; it does not prove every call path on the target head unit or identify the Binder/JNI provider by itself.

## Recovered calls

| API call | Arguments observed | Notes |
|---|---|---|
| `setTreble(value, max)` | `max = Integer.MAX_VALUE` during direct updates and boot restore | The second argument is a large sentinel in this app; do not assume it is a hardware maximum without the wrapper implementation. |
| `setBassBoost(value, max)` | `max = 200` during direct updates; `100` during boot restore | The app uses different second-argument values across paths. |
| `setSurround(value)` | One integer | Exact range is not established by this callsite. |
| `setLoud(value)` | Stored setting; boot restore normalizes to `0` or `1` | Boolean-like use in boot path. |
| `setSubWoofer(enabled, value)` | Direct update: second arg `20`; boot restore: `200`; enabled normalized to `0` or `1` | The second argument is not constant across paths. |
| `setBalance(fl, fr, rl, rr)` | Four integer levels | Values are computed from the balance UI and scaled into the 0..15 domain. |
| `setMasterVolume(left, right)` | Each channel normalized by dividing an integer level by `15.0f` | Separate high-level API call made after balance update when ASP is present. |

`AspSettings.setAspBalance()` converts the two-dimensional balance coordinates into FL/FR/RL/RR integer levels using integer arithmetic and a 0..15 scale. The four values are passed directly to `AudioEffect.setBalance()`. This matches the CD3313 driver's four speaker-level index domain, but it does not prove that all other app paths use the same range.

`AspSettings.startBootAspSetting()` replays treble, bass, balance, loudness and subwoofer settings at boot. These calls are a real startup caller path, not just UI-only methods.

## Calls not found in this APK slice

The inspected `AspSettings` class does not call `setInputGain()` or `setMasterVolumeMax()`. This is a scoped negative finding for the inspected APK, not proof that those APIs have no other callers in the full firmware.

## Engineering consequences

- Preserve the observed `0..15` domain for four-channel balance.
- Do not collapse the second arguments of treble, bass or subwoofer methods into one global range; the app passes different values on direct-update and boot paths.
- Treat the `Integer.MAX_VALUE` treble argument as a caller sentinel until the Java wrapper and vendor callback semantics are fully correlated.
- The app normalizes loudness/subwoofer enable values to 0/1 during boot restore; retain explicit boolean validation in the replacement API.
- Keep the JNI extra-word mismatch separate from these Java API values. The native bridge adds one word to `_IOC_SIZE` beyond the Java array length, and the extra word is not a documented application argument.

No app code was executed. No Binder transaction, ioctl, device-node open, I2C write or hardware state change was performed.
