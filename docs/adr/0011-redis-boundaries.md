# 0011 — Redis usage boundaries

**Status:** Accepted
**Date:** 2026-07-11

## Context

Real-time session coordination (which gateway owns a stream, presence, rate limits) needs
low-latency shared state that would be wasteful in PostgreSQL.

## Decision

Redis is permitted for ephemeral session state, distributed locks, rate limiting, caching,
presence, and short-lived coordination — always with TTLs. Redis is **never** the authoritative
store: any Redis loss must be recoverable by reconnect/re-registration against PostgreSQL-backed
state. No durable business data, no audit data, no queue-of-record in Redis.

## Alternatives considered

- **Postgres for everything (advisory locks, unlogged tables):** viable at small scale; hot
  session state and rate limiting would contend with transactional load.
- **In-process caches only:** breaks with multiple gateway replicas.

## Consequences

Every Redis key gets a TTL and a documented reconstruction path; flushing Redis in staging is a
required chaos test; services must degrade gracefully (slower, not wrong) without Redis.

## Security implications

Session tokens/ephemeral credentials in Redis are short-lived and encrypted in transit; Redis
AUTH + network isolation; no transcript or audio content ever cached in Redis.

## Operational implications

Managed Redis with replication is sufficient (no persistence guarantees needed by design).

## Migration or rollback plan

By construction: contents are disposable; swapping to another cache (e.g. Valkey) touches
connection config and lock/cache adapters only.

## References

Master prompt §5.3. ADR-0010.
