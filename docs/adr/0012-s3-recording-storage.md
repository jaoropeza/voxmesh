# 0012 — S3-compatible recording storage

**Status:** Accepted
**Date:** 2026-07-11

## Context

Recordings are large, immutable-after-finalization blobs with lifecycle, residency, legal-hold,
and resumable-upload requirements. They do not belong in PostgreSQL.

## Decision

All recordings, per-track media files, and large derived artifacts (exports) live in
S3-compatible object storage behind a storage interface. Uploads are resumable (multipart),
content-checksummed, and idempotent; finalization is atomic (upload → verify → commit reference in
PostgreSQL → only then allow desktop spool cleanup). Bucket/prefix layout encodes tenant and
residency; lifecycle rules implement retention classes per track type (ADR-0006).

## Alternatives considered

- **Filesystem/NFS volumes:** no lifecycle/residency/multipart semantics, painful horizontal
  scaling.
- **Database blobs:** backup bloat and throughput ceilings.

## Consequences

PostgreSQL stores references + checksums, never media; every media consumer takes a signed,
time-limited URL or streams via a service — no public objects; MinIO serves as the S3-compatible
dev/test target.

## Security implications

Server-side encryption with tenant-scoped keys where required; signed URLs short-lived and
audit-logged; legal hold implemented via object lock/immutability policies; checksums detect
tampering.

## Operational implications

Residency = bucket placement; storage metrics (`upload_backlog_bytes`) feed capacity planning.

## Migration or rollback plan

S3 API is a de-facto standard; provider moves are data-copy exercises behind the storage
interface.

## References

Master prompt §4, §11, §18. ADR-0006, ADR-0016.
