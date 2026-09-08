# Verification cadence contract

CPU and GPU workloads use the same two independent controls:

- `verify_interval`: perform an actual checksum/readback verification every N completed work units (`batch` for CPU, `frame` for GPU). It must be greater than zero when verification is enabled.
- `success_log_interval`: emit the first successful `verify` event and then one event after every N additional successful verifications. `0` suppresses successful verify events.

A failed verification is always emitted immediately and is never suppressed by `success_log_interval`. The final `summary` always reports `verify_pass`, `verify_count`, `verify_fail_count`, and the last checksum, even when successful verify events are suppressed.

The deprecated `checksum_interval`/`--checksum-interval` spelling remains accepted as an alias for `verify_interval`. Its meaning is now identical for CPU and GPU. New configurations must use the canonical names.

Recommended qualification settings:

```json
{
  "verify_interval": 1,
  "success_log_interval": 60
}
```

This checks every work unit while bounding successful diagnostic output. Reducing log volume must be done with `success_log_interval`, not by weakening `verify_interval`.
