# 0017 — GitHub Flow and branch protection

**Status:** Accepted
**Date:** 2026-07-11

## Context

Multiple humans and AI agents work in parallel on one monorepo (ADR-0001). Long-lived integration
branches multiply merge conflicts exactly where agents are most error-prone.

## Decision

Trunk-based development via GitHub Flow: short-lived branches named
`<type>/<issue>-<slug>` off `main`; every change through a PR; squash merge; linear history;
`main` always releasable; feature flags for incomplete behavior; semver release tags.
Classic GitFlow (permanent `develop`/`release`/`hotfix`) is rejected. `release/*` branches only
when a shipped version genuinely needs separate maintenance; `hotfix/*` only for supported
production releases that cannot wait for mainline. Required protections are specified in
`docs/development/branch-protection.md`.

## Alternatives considered

- **GitFlow:** permanent divergence between `develop` and `main` creates merge debt and delayed
integration; built for scheduled big-bang releases this project does not have.
- **Direct-to-main (no PRs):** incompatible with review requirements and AI-generated code
  quality gates.

## Consequences

Incomplete features must ship dark behind flags; branch protection enforcement depends on CI
check names staying in sync with workflow files; issue numbers become mandatory before coding
starts.

## Security implications

No direct pushes + CODEOWNERS + two-approval rule for sensitive paths form the review control;
admin bypass only via documented emergency process.

## Operational implications

Squash merges keep `git bisect` usable; release tagging is the deployment trigger.

## Migration or rollback plan

Process decision; revisit via superseding ADR if release-train needs emerge.

## References

Master prompt §20–21. `CONTRIBUTING.md`, `docs/development/branch-protection.md`.
