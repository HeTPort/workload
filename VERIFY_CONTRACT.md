# Verification cadence contract

CPU and GPU workloads use the same two independent controls:

- `verify_interval`: perform an actual checksum/readback verification every N completed work units (`batch` for CPU, `frame` for GPU). It must be greater than zero when verification is enabled.
- `success_log_interval`: emit the first successful `verify` event and then one event after every N additional successful verifications. `0` suppresses successful verify events.

A failed verification is always emitted immediately and is never suppressed by `success_log_interval` or `summary_only`. Verify events contain boolean `pass` and string `result` ("PASS"/"FAIL"). The final summary reports `contract_version: 2`, effective verify settings, `verify_pass`, `verify_count`, `verify_fail_count`, and the last checksum.

Successful verify events additionally have a fixed minimum spacing of 1000 ms. The first selected success emits immediately; later interval-selected candidates inside that time are dropped, never queued. This bounds success output to at most one event per second without reducing checks. Failures bypass both gates. Explicit per-frame/per-batch diagnostics are not rate limited and should remain disabled for live qualification.

`summary_only` suppresses start, successful verify and per-work-unit diagnostics; it preserves workload heartbeats, failed verify, error, golden and summary. It is therefore no longer literally a single-line output mode. Heartbeats represent actual workload progress; no synthetic heartbeat thread masks a stuck backend.

The deprecated `checksum_interval`/`--checksum-interval` remains an alias for `verify_interval`. Multiple spellings within the same config or command line are rejected, including mixed underscore/hyphen spellings. CLI overrides config regardless of which spelling the two layers use. New configurations must use the canonical names.

Migration warning: CPU releases before this contract interpreted `checksum_interval` only as a success-log interval, while verifying every batch. A legacy CPU value greater than 1 now reduces coverage. Migrate it to `verify_interval: 1` and the desired `success_log_interval` explicitly.

Both binaries provide `--capabilities`, printing one JSON line and exiting 0 without initializing a backend:

```json
{"workload":"cpu","version":"2.1.0","contract_version":2,"features":["verify_interval","success_log_interval","verify_count","failure_verify","live_heartbeat","success_log_rate_limit"],"success_log_min_period_ms":1000,"verify_modes":["none","checksum","crc"]}
```

GPU uses `workload: "gpu"` and modes `none, crc, checksum, golden-image, pixel-diff, compute-compare`. Capabilities describe parser/verifier support; backend availability depends on build flags and device. A normal verified run reports `floor(completed_work_units / verify_interval)` checks. Golden generation is a separate one-work-unit capture and is not proof of sustained coverage. GPU crc/checksum without external golden records a checksum only and must not be accepted as correctness qualification.

Recommended qualification settings:

```json
{
  "verify_interval": 1,
  "success_log_interval": 60
}
```

This checks every work unit while bounding successful diagnostic output. Reducing log volume must be done with `success_log_interval`, not by weakening `verify_interval`.
