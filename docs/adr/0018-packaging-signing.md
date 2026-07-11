# 0018 — Packaging and signing strategy

**Status:** Accepted
**Date:** 2026-07-11

## Context

Enterprise desktop deployment demands signed, silently installable, upgradeable packages per
platform (master prompt §2, §29), produced reproducibly by CI, with signing credentials protected
from untrusted workflows.

## Decision

- **Windows:** MSI (WiX) as primary; MSIX where enterprise deployment requires it; Authenticode
  signing; silent-install parameters; documented upgrade/rollback and uninstall behavior.
- **macOS:** app bundle (universal where practical), PKG + DMG, Developer ID signing, hardened
  runtime, entitlements, notarization, clear permission onboarding.
- **Linux:** DEB (Ubuntu 22.04/24.04/26.04) + AppImage, desktop entry + icons, PipeWire
  dependency checks, clear diagnostics on unsupported audio environments.

All packaging is automated in CI. Signing/notarization run only in protected GitHub environments
(`code-signing`, `apple-notarization`) on protected refs — never on untrusted PR workflows.
Prefer OIDC-federated access to signing services over long-lived secrets.

## Alternatives considered

- **NSIS (Windows):** weaker enterprise/GPO story than MSI.
- **Snap/Flatpak (Linux):** sandbox models complicate system-audio capture; revisit by ADR if
  demand appears.
- **Manual signing:** irreproducible and audit-hostile.

## Consequences

Per-platform packaging code under `packaging/`; upgrade-path tests (old→new silent upgrade) join
the release checklist; LGPL relink-ability constraints from ADR-0003 shape how Qt libraries are
bundled.

## Security implications

Signing keys are the crown jewels: environment-scoped, access-audited, hardware-backed where
possible (HSM/KMS); SBOM ships with releases.

## Operational implications

Apple notarization adds minutes and an external dependency to release pipelines; plan retries.

## Migration or rollback plan

Installers must support downgrade paths or side-by-side recovery; format changes (MSI→MSIX-only)
gated by enterprise-customer validation.

## References

Master prompt §2, §21, §28, §29. ADR-0003.
