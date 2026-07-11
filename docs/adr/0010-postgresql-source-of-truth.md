# 0010 — PostgreSQL as source of truth

**Status:** Accepted
**Date:** 2026-07-11

## Context

The control plane owns tenants, users, roles, meetings, provider credentials, retention policies,
quotas, audit records, and workflow status — relational, transactional, audited data.

## Decision

PostgreSQL is the single source of truth for control-plane state. Access via EF Core; Dapper only
where profiling demonstrates need. Redis (ADR-0011), Kafka (ADR-0009), object storage (ADR-0012),
and the vector store (ADR-0013) are derived or specialized stores — any of them must be
reconstructible from PostgreSQL plus object storage.

## Alternatives considered

- **Document DB (Mongo/Cosmos):** multi-tenant relational integrity (FKs across tenants/users/
  meetings/policies) and audit queries are the core workload; relational wins.
- **Per-service databases now:** premature; one well-modeled schema with clear module boundaries
  first, split later if service extraction demands it.

## Consequences

Migrations are first-class (EF Core migrations, reviewed like code); every table carries
`tenant_id` with enforced tenant scoping (ADR-0020); read replicas for search/metadata queries
when needed.

## Security implications

Encryption at rest; provider credentials/secrets stored encrypted (envelope encryption via the
secret store, not plaintext columns); row-level tenant isolation is a security control, not a
convention.

## Operational implications

Backup/PITR, residency-aware placement of instances per data-residency requirements.

## Migration or rollback plan

Standard relational migration tooling; derived stores rebuild from Postgres + object storage by
replaying indexing workflows.

## References

Master prompt §4, §5.3. ADR-0011, ADR-0020.
