# Contracts

Versioned interface contracts shared across components. Protobuf packages live under `proto/`
(`voxmesh.<domain>.v1`); OpenAPI specs for external REST APIs land under `openapi/` in Phase 3.

Rules (enforced by `buf` in CI):

- `buf lint` (STANDARD) must pass.
- `buf breaking` against `main` must pass — a breaking change requires a new package version
  (`v2`) or an explicit, documented migration strategy approved via ADR.
- Never reuse or renumber field numbers; `reserve` removed fields.
- Provider-specific (Teams/Meet/Zoom) shapes stay in their adapters, out of shared contracts.

Local check:

```sh
npx @bufbuild/buf lint libs/contracts/proto
npx @bufbuild/buf breaking libs/contracts/proto --against '.git#branch=main,subdir=libs/contracts/proto'
```

Language codegen (C++/C#/Python/TypeScript) is wired per consumer in later phases; this package
is the single schema source.
