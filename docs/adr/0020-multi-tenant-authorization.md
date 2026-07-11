# 0020 — Multi-tenant authorization model

**Status:** Accepted
**Date:** 2026-07-11

## Context

Every stored artifact (meetings, recordings, transcripts, summaries, embeddings) belongs to a
tenant, and cross-tenant access is the top named threat model. Search must filter by permissions
at retrieval time (master prompt §17, §18).

## Decision

- **Identity:** OIDC federation with enterprise SSO; users belong to exactly one tenant context
  per session; service principals are tenant-scoped or explicitly platform-level.
- **Authorization layers:** (1) tenant isolation — every table, topic payload, object key, and
  vector record carries `tenant_id`, and every query path is tenant-scoped by construction
  (enforced in repositories/middleware, with PostgreSQL row-level security as defense in depth);
  (2) RBAC within a tenant (admin, compliance, member, …); (3) meeting-level permissions
  (owner/participant/shared) evaluated for content access.
- **Retrieval-time enforcement:** search and vector queries embed tenant + ACL filters in the
  query itself; post-filtering results is prohibited.
- **Audit:** every authorization-relevant decision on sensitive content is auditable.

## Alternatives considered

- **Database-per-tenant:** strongest isolation, operationally heavy at scale and painful for
  managed onboarding; revisit for regulated single-tenant deployments via ADR.
- **Central policy engine (OPA/SpiceDB) now:** attractive for meeting-level sharing graphs;
  deferred until sharing semantics stabilize — the interface (an authorization service in the
  control plane) leaves room for it.

## Consequences

`tenant_id` is a required field on contracts (already in `AudioFrame`); authorization checks are
middleware/repository concerns with conformance tests attempting cross-tenant access; consent and
retention policies attach at tenant and meeting level.

## Security implications

This ADR is the implementation anchor for the cross-tenant and vector-bypass threat models;
penetration tests target exactly these seams.

## Operational implications

Tenant offboarding = keyed deletion workflow across Postgres, object storage, vector store, and
Kafka retention windows.

## Migration or rollback plan

Moving to a policy engine later replaces the decision internals of the authorization service, not
its call sites.

## References

Master prompt §5.3, §17, §18. ADR-0010, ADR-0013.
