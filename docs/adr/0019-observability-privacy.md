# 0019 — Observability and privacy boundaries

**Status:** Accepted
**Date:** 2026-07-11

## Context

Diagnosing a distributed real-time pipeline requires correlation from desktop session to search
result, but the payloads flowing through it (audio, transcripts, names) are exactly what must not
leak into telemetry (master prompt §30).

## Decision

OpenTelemetry everywhere (C++, C#, Python, TypeScript) with a common correlation context:
desktop session → media stream → STT session → transcript segment → translation → summary →
embedding → search result. Prometheus-compatible metrics (the §30 catalog is the baseline),
centralized structured logs, distributed tracing. **Privacy boundary:** no raw audio, transcript
text, participant names, access tokens, or private meeting content in metrics, logs, or traces by
default. IDs, counts, durations, and enum states only. Content-bearing diagnostics require an
explicit, audited, time-limited debug flag per tenant.

## Alternatives considered

- **Vendor APM SDKs:** lock-in at every instrumentation site; OTel keeps exporters swappable.
- **Log-everything-then-scrub:** scrubbing fails open; the default must be content-free.

## Consequences

Loggers take typed fields, not free-form interpolated strings, so reviews can spot content leaks;
every dropped/late frame emits a metric and structured event (master prompt §8); correlation IDs
propagate through gRPC metadata, Kafka headers, and workflow context.

## Security implications

Telemetry pipelines are attack surface for data exfiltration — they carry only content-free
records by construction; audit trail records use a separate, access-controlled store.

## Operational implications

Collector deployment per environment; cardinality budgets for tenant-labeled metrics.

## Migration or rollback plan

OTel abstracts exporters; backend swaps are configuration.

## References

Master prompt §8, §18, §30.
