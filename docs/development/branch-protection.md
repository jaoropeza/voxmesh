# Branch protection for `main`

These rules come from the master prompt §20–21 and ADR-0017. They must be applied in GitHub
repository settings (or via the API) by an admin; this document is the source of truth for what
should be configured. Re-check after changing CI workflow names, because required status checks
are matched by name.

## Required settings (Settings → Branches → Branch protection rules → `main`)

- Require a pull request before merging.
  - Require at least **1 approval** for ordinary changes.
  - **Dismiss stale pull request approvals** when new commits are pushed.
  - Require review from **Code Owners**.
- Require status checks to pass before merging, and require branches to be up to date:
  - `cpp / build-test (ubuntu-22.04)`
  - `cpp / build-test (ubuntu-24.04)`
  - `cpp / build-test (windows-latest)`
  - `cpp / build-test (macos-15)`
  - `dotnet / build-test`
  - `python / lint-type-test`
  - `contracts / buf`
- Require conversation resolution before merging.
- Require linear history (squash merge only; disable merge commits in repo settings).
- Do not allow force pushes.
- Do not allow deletions.
- Enforce for administrators (bypass only through a formally invoked emergency process,
  documented in the audit trail).
- Enable signed-commit requirement if/when organizational policy requires it.

## Two-approval rule

Changes touching security, authentication, encryption, media capture, real-time audio, contracts,
or persistence require **2 approvals**. GitHub branch protection supports only one global
approval count, so this is enforced by:

1. CODEOWNERS routing those paths to the owning teams (see `.github/CODEOWNERS`), and
2. Reviewer discipline documented in `CONTRIBUTING.md` — do not merge such PRs with one approval.

Revisit with GitHub rulesets (which support path-scoped required reviews) once teams exist.

## Environments

Create GitHub environments with required reviewers and scoped secrets for: `development`,
`staging`, `production`, `code-signing`, `apple-notarization`. Signing credentials must never be
exposed to untrusted pull-request workflows — signing jobs run only on protected branches/tags
with environment protection. Use OIDC for cloud authentication instead of long-lived credentials
wherever supported.
