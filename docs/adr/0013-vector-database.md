# 0013 — Vector database selection

**Status:** Accepted
**Date:** 2026-07-11

## Context

Semantic search indexes segment-, chapter-, and meeting-level embeddings with mandatory
retrieval-time filters (tenant, participants, dates, language, provider, ACLs). Business logic
must depend on an `IVectorStore` interface, never a vendor SDK (master prompt §4, §17).

## Decision

**Qdrant** is the first `IVectorStore` adapter. Rationale against the required comparison set:

- **Qdrant:** strong filtered-search performance (payload indexes evaluated during traversal, not
  post-filtered — this is the workload's defining feature), first-class metadata payloads,
  gRPC + REST, simple self-hosted deployment on K8s, permissive license.
- **Milvus:** excellent scale ceiling but heaviest operational footprint (etcd/Pulsar/MinIO
  components); overkill before scale demands it.
- **PostgreSQL + pgvector:** operationally free (already run Postgres) and transactionally
  consistent, but HNSW + heavy metadata filtering at multi-tenant scale is weaker; remains the
  fallback adapter candidate for small self-hosted deployments.
- **OpenSearch/Elasticsearch:** attractive because hybrid lexical+vector lives in one engine;
  heavier cluster ops and weaker pure-ANN performance; remains a candidate for the lexical side
  of hybrid search regardless.

## Alternatives considered

Covered above, per the spec's required comparison.

## Consequences

Every vector record carries tenant ID, meeting ID, source segment/chapter ID, embedding model +
version + dimensions, language, ACL metadata, and creation timestamp. Changing embedding models
triggers versioned reindexing (new collection per model version, cut over atomically). Hybrid
ranking merges lexical and vector results above the store.

## Security implications

Tenant/ACL filters are applied **inside** the vector query (retrieval-time authorization);
a vector-search authorization bypass is a named threat model — adapter tests must prove filters
cannot be omitted.

## Operational implications

Qdrant runs as a stateful K8s workload with snapshots; collections partitioned by tenant shard
strategy documented at implementation time.

## Migration or rollback plan

Embeddings are derived data: stand up the new store, replay indexing from PostgreSQL + object
storage, flip the adapter. No unique data lives in Qdrant.

## References

Master prompt §4, §17. ADR-0015, ADR-0020.
