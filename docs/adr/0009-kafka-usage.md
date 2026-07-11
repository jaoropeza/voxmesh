# 0009 — Kafka event usage boundaries

**Status:** Accepted
**Date:** 2026-07-11

## Context

The async plane (final transcripts, translation, summarization, embeddings, indexing, exports)
needs durable, replayable, fan-out events. The real-time plane needs single-digit-to-low-hundreds
ms latency. One technology must not be forced to serve both.

## Decision

Kafka carries **durable domain events and async workflows only**: e.g. `meeting.finalized`,
`transcript.segment.finalized`, `recording.uploaded`, reprocessing requests. Kafka never carries
raw audio frames or live partial transcripts; those use gRPC streams (ADR-0008) and
WebSocket/SSE. Events reference large payloads in object storage (ADR-0012) rather than embedding
them.

## Alternatives considered

- **Kafka everywhere:** simple mental model, unacceptable hot-path latency and broker load from
  audio frames.
- **RabbitMQ/NATS for events:** fine brokers, but replayable log semantics (reindex, reprocess)
  are exactly Kafka's model and the spec mandates Kafka.

## Consequences

Clean separation: losing Kafka degrades async processing but never live capture/transcription;
consumers must be idempotent (replay is a feature); topic naming and schema governance follow the
contracts package.

## Security implications

Topics carry meeting metadata and transcript references — encrypt in transit, restrict ACLs per
service principal, keep payloads content-free where possible (IDs + storage pointers).

## Operational implications

Consumer-lag monitoring (`embedding_queue_lag`); retention per topic aligned with reprocessing
windows and tenant retention policies.

## Migration or rollback plan

Event publication is behind a thin publisher interface in each service; the log semantics are the
sticky part — migration would require replay-window planning.

## References

Master prompt §4, §5.2, §5.4. ADR-0008, ADR-0014.
