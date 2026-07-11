# 0016 — Desktop local spool encryption

**Status:** Accepted
**Date:** 2026-07-11

## Context

The recorder keeps capturing through network loss (master prompt §11), buffering audio locally
until the server acknowledges upload. A stolen laptop or copied spool directory must not yield
meeting audio ("stolen local spool" is a named threat model).

## Decision

The local spool is always encrypted: AES-256-GCM per chunk, with data keys wrapped by a key held
in the OS keystore (DPAPI on Windows, Keychain on macOS, Secret Service/libsecret on Linux).
Spool properties: bounded size and retention (configurable), content checksums, idempotent
resumable upload, crash-safe (write-ahead chunk manifest, atomic finalization), cleanup only
after server acknowledgement, explicit low-disk handling, user-visible capture/upload state.
Encryption keys never appear in logs, config files, or crash dumps.

## Alternatives considered

- **Plaintext spool + OS full-disk encryption:** FDE is not guaranteed on enterprise Linux/BYOD
  fleets and does not protect a copied directory on a running system.
- **No spool (stream-only):** violates the resilience requirement; network loss would drop audio.

## Consequences

Key loss makes an unuploaded spool unrecoverable — acceptable by design (server copy is the
durable one; the spool is a buffer, not an archive); crash recovery tests must cover mid-chunk
truncation.

## Security implications

Threat coverage: stolen spool (encrypted), tampering (GCM auth tags + checksums), partial-upload
replay (idempotency keys). Key rotation applies to the wrapping key.

## Operational implications

Spool metrics (`local_spool_bytes`, `upload_backlog_bytes`) surface stuck uploads; support
tooling can enumerate manifest state without decrypting content.

## Migration or rollback plan

Chunk format carries a version header; format changes drain naturally as spools upload within
retention windows.

## References

Master prompt §11, §18. ADR-0012.
