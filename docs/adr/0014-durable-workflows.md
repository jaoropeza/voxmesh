# 0014 — Durable workflow engine

**Status:** Accepted
**Date:** 2026-07-11

## Context

Post-meeting processing is a multi-step, long-running, failure-prone pipeline (normalize →
translate → diarize → summarize → embed → index → export) needing retries, resumption,
versioning, and observable state (master prompt §5.4).

## Decision

Use **Temporal** as the durable workflow engine for the async AI/indexing plane. Workflows
orchestrate; workers in Python (AI) and C# (control plane) execute activities. Kafka events
trigger workflow starts; workflow state is not duplicated into business tables beyond a status
projection for the UI.

## Alternatives considered

- **Hand-rolled state machines on Kafka + DB:** every team reinvents retries, timers, and
  versioning badly; high defect surface exactly where the spec demands durability.
- **AWS Step Functions / cloud-native orchestrators:** conflicts with self-hosted and
  data-residency requirements.
- **Argo Workflows:** batch/K8s-job oriented; weak for long-lived, signal-driven, versioned
  business workflows.

## Consequences

Temporal cluster (or Temporal Cloud where residency allows) joins the infrastructure baseline;
activities must be idempotent; workflow code follows determinism rules; reprocessing = starting a
new workflow version over stored inputs.

## Security implications

Workflow histories can contain arguments — pass IDs and storage references, never transcript
content or credentials, as workflow inputs.

## Operational implications

Temporal UI gives per-meeting pipeline visibility; workflow metrics feed the observability stack.

## Migration or rollback plan

Activities are plain functions behind interfaces; the orchestration layer is the replaceable
part. A projection table of workflow status decouples the UI from Temporal APIs.

## References

Master prompt §5.4. ADR-0009.
