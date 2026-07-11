# 0001 — Monorepo strategy

**Status:** Accepted
**Date:** 2026-07-11

## Context

VoxMesh spans a C++ desktop recorder, .NET control plane, Python AI services, a web app, shared
protobuf contracts, infra, and packaging. Contracts and cross-cutting changes (e.g. a new
transcript field) touch several components at once. The team is small and iterating quickly.

## Decision

Use a single monorepo with the layout in master prompt §19 (`apps/`, `libs/`, `platform/`,
`services/`, `infra/`, `packaging/`, `tests/`, `docs/`). Split a component into its own repository
only when ownership, release cycles, or security boundaries are genuinely independent, build times
become unreasonable, or the component is reused by unrelated products.

## Alternatives considered

- **Repo per service:** clean ownership, but atomic contract changes become multi-repo dances and
  version skew appears immediately. Wrong trade-off for a small team pre-1.0.
- **Two repos (desktop vs backend):** still splits the contracts package or duplicates it.

## Consequences

Atomic cross-component PRs and one CI entry point; per-path CODEOWNERS substitutes for repo-level
ownership; CI must filter by path to keep pipelines fast as the repo grows.

## Security implications

Repository access grants visibility into all components; use environment-scoped secrets and
path-scoped CODEOWNERS review for sensitive areas.

## Operational implications

One issue tracker and release tagging scheme; release tags are semver per product
(e.g. `desktop-v0.2.0`) when release cadences diverge.

## Migration or rollback plan

Extract a component with `git filter-repo` preserving history; contracts package would be
published as a versioned artifact first.

## References

Master prompt §19.
