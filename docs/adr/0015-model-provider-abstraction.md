# 0015 — Model-provider abstraction

**Status:** Accepted
**Date:** 2026-07-11

## Context

STT, translation, diarization, summarization, and embeddings must work with self-hosted NVIDIA/
Triton models and managed providers, routed per tenant with fallback, quotas, residency, and cost
controls (master prompt §13). Enterprise customers will demand provider substitution.

## Decision

Business logic depends only on the provider interfaces: `IStreamingSpeechToTextProvider`,
`IBatchSpeechToTextProvider`, `ITranslationProvider`, `IDiarizationProvider`,
`ISummarizationProvider`, `IEmbeddingProvider`, `IVectorStore`. A routing layer resolves
tenant policy → concrete provider + model, applying fallback chains, circuit breakers, quotas,
residency constraints, and cost limits. Every produced artifact records model provider + version.
Model names never appear in business logic — only in provider configuration.

## Alternatives considered

- **Direct SDK usage per feature:** fastest to first demo, then every provider change is a
  cross-cutting rewrite; disqualified by requirements.
- **Gateway-only abstraction (e.g. a LiteLLM-style proxy):** helps for chat-completion-shaped
  APIs; does not cover streaming ASR, diarization, or vector stores. May still be used *inside*
  an adapter.

## Consequences

Each provider is an adapter package with conformance tests against the interface contract;
adding a provider touches configuration + one adapter, nothing else; routing policy is
control-plane data (PostgreSQL), not code.

## Security implications

Provider credentials are tenant-scoped secrets with least privilege; residency constraints are
enforced by the router (a residency-violating fallback must fail closed, not fall through).

## Operational implications

Per-provider metrics (latency, error rate, cost) drive circuit breakers; model-version recording
enables output provenance and regression triage.

## Migration or rollback plan

The abstraction *is* the migration plan: providers are swappable per tenant at policy level.

## References

Master prompt §13, §14, §16, §17. ADR-0013.
