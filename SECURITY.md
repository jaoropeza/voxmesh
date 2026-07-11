# Security Policy

VoxMesh processes meeting audio, transcripts, summaries, and embeddings. All of these are
treated as sensitive data.

## Reporting a vulnerability

Report vulnerabilities privately via GitHub Security Advisories on this repository
(**Security → Report a vulnerability**). Do not open public issues for security problems.
You should receive an acknowledgement within 5 business days.

## Scope

Of particular interest, matching the project threat models (master prompt §18):

- Cross-tenant data access.
- Vector-search authorization bypass.
- Token theft or credential leakage.
- Compromised desktop agent or stolen local spool.
- Prompt injection through transcript content.
- Malicious media files.
- Dependency compromise.

## Handling rules for contributors

- Never commit secrets, tokens, production data, customer recordings, or sensitive transcripts —
  including inside test fixtures and AI prompts.
- Do not log audio, transcript text, participant names, or access tokens; structured logging must
  stay content-free by default.
- Security-sensitive changes require two approvals (see `docs/development/branch-protection.md`).
