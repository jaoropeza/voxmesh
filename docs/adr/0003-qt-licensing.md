# 0003 — Qt licensing review

**Status:** Proposed — pending legal review
**Date:** 2026-07-11

## Context

Qt 6 is dual-licensed: LGPL-3.0 (most modules) / commercial. VoxMesh is proprietary enterprise
software distributed as signed desktop installers on three platforms. LGPL obligations (dynamic
linking, relink ability, license notices, no LGPL-incompatible modifications) and module-level
exceptions (some Qt modules are GPL-only or commercial-only) materially affect packaging and
distribution.

## Decision

Proceed with Qt 6 development under the working assumption of **LGPL-3.0 compliance via dynamic
linking**, while legal review determines whether a commercial Qt license is required or preferred.
Until legal review concludes:

- Link Qt dynamically only; never statically.
- Use only LGPL-licensed Qt modules; check each added module's license before use.
- Do not modify Qt sources.
- Keep the dependency inventory in `LICENSES/README.md` current.

This ADR intentionally makes **no legal conclusion**; it records the engineering posture that
keeps both outcomes open.

## Alternatives considered

- **Commercial license now:** cost before revenue; may still be chosen by legal/business.
- **Avoid Qt entirely:** rejected in ADR-0002 on engineering grounds.

## Consequences

Installer/packaging work must preserve relink ability (shipped Qt libs replaceable by the user);
module choices are constrained until review completes.

## Security implications

Dynamic Qt libraries must be signed and integrity-checked as shipped; update channel must not
allow Qt library substitution attacks while still honoring LGPL relink rights — this tension is a
specific question for legal review.

## Operational implications

Qt version upgrades tracked like any pinned dependency; license inventory updated per release.

## Migration or rollback plan

If commercial licensing is chosen, remove LGPL-driven constraints. If LGPL is confirmed, document
the compliance checklist in `docs/security/` and packaging docs.

## References

Master prompt §3. `LICENSES/README.md`. Flagged for legal review — do not close without it.
