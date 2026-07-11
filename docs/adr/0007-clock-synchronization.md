# 0007 — Clock synchronization and drift correction

**Status:** Accepted
**Date:** 2026-07-11

## Context

Microphone and speaker devices run on different hardware clocks that drift relative to each other
(tens of ppm is normal). Multi-hour meetings accumulate audible desync if tracks are aligned by
callback arrival order or wall-clock time.

## Decision

Per master prompt §9: timestamp every frame with a monotonic clock; preserve native device
timestamps when available; maintain independent per-track sequence numbers; select a master
timeline; detect drift continuously (`clock_drift_ppm` metric); correct via gradual resampling or
sample-level correction; insert explicit silence for unrecoverable gaps; record discontinuity
metadata on the frame (`discontinuity` flag in the AudioFrame contract). Alignment by callback
arrival order is prohibited.

## Alternatives considered

- **Trust callback order / wall clock:** fails under scheduler jitter, device restarts, DST/NTP
  steps; the prohibited baseline.
- **Hard resync (drop/duplicate blocks):** audible artifacts; reserved for unrecoverable gaps
  only, and then explicit and recorded.

## Consequences

A dedicated synchronization stage between capture and encoding; deterministic simulation tests are
mandatory (positive/negative drift, device restart, delayed/dropped/duplicate/out-of-order
frames); Phase 1's fake backend must be able to inject all of these.

## Security implications

None direct; discontinuity metadata supports forensic integrity of recordings (gaps are provable
rather than silent).

## Operational implications

`clock_drift_ppm`, `audio_discontinuities`, and `device_reconnects` become first-class metrics
with alerting in soak tests.

## Consequences for testing

Soak runs (30 min / 2 h / 8 h) measure residual drift as a release gate.

## Migration or rollback plan

Correction strategy (resample vs sample-slip) sits behind the sync stage interface and can be
swapped per measurement.

## References

Master prompt §9, §27, §30. ADR-0006.
