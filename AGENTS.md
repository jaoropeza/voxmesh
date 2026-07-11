# AGENTS.md — VoxMesh development contract

Tool-neutral rules for every contributor, human or AI (Claude Code, Cursor, OpenAI Codex).
Tool-specific extensions live in `CLAUDE.md` and `.cursor/rules/`; they must not contradict this
file. The authoritative product specification is
`docs/architecture/voxmesh-master-prompt.md`; material decisions are ADRs in `docs/adr/`.

## Ground rules

1. Work in small, testable vertical slices. Never attempt broad multi-subsystem changes in one PR.
2. Before modifying code: read this file, the relevant ADRs, and the task's GitHub issue with its
   acceptance criteria. Present a concise plan before large implementations.
3. Record material architectural decisions as ADRs (`docs/adr/`, format in master prompt §25).
4. Ask a question only when a missing decision makes correct implementation impossible. Otherwise
   choose the safest reasonable default, document the assumption, and continue.
5. Pin exact dependency versions everywhere. Never depend on floating versions such as `latest`.
6. Never commit secrets, production data, customer recordings, or sensitive transcripts — in code,
   fixtures, logs, or AI prompts.
7. AI-generated code meets the same standards as human-written code. Never bypass tests, reviews,
   or static analysis because an AI produced the change.

## Architecture invariants

Violating these requires a superseding ADR, not a code comment:

- Four planes (master prompt §5): desktop/provider ingress, real-time media, control plane,
  async AI/indexing. Provider-specific schemas never leak past their adapters.
- The latency-critical audio path is capture → gRPC → media gateway → streaming STT → live
  transcript → WebSocket/SSE. Kafka carries durable events only, never raw audio frames.
- The common C++ audio core contains no platform APIs (WASAPI, ScreenCaptureKit/Core Audio,
  PipeWire live under `platform/`). No Objective-C types leak into common C++ APIs.
- Original microphone and speaker tracks are separate, synchronized, and never discarded because
  a derived mix exists. Tracks are never aligned by callback arrival order.
- Transcript segments are revisioned: partials replace the mutable revision of the same segment;
  they are never appended as independent final records.
- Business logic depends on provider interfaces (`IStreamingSpeechToTextProvider`, `IVectorStore`,
  …) — never on vendor SDKs or hard-coded model names.
- Translations never replace originals; inferred diarization labels are never silently promoted
  to confirmed participant identities; extracted decisions/action items carry evidence segment IDs.
- Search authorization is enforced at retrieval time, not by post-filtering.
- PostgreSQL is the source of truth; Redis is ephemeral coordination only.
- No audio, transcript text, participant names, or tokens in logs or metrics by default.

## Language standards

**C++ (C++20, CMake ≥ 3.28, Conan 2, GoogleTest):** RAII; value semantics; `std::unique_ptr` for
ownership (`shared_ptr` only when shared ownership is intentional and documented); `std::span` for
non-owning buffers; `std::chrono` and strong types for time, rates, and sequence numbers; bounded
queues; explicit thread ownership; stop tokens; deterministic shutdown. Prohibited: owning raw
pointers, unbounded queues, detached threads, uncontrolled global mutable state, exceptions
crossing C APIs/platform callbacks/thread boundaries, blocking work in audio callbacks, silent
audio loss. Real-time callback rules are in master prompt §8. `clang-format` and `clang-tidy`
clean; warnings are errors for project-owned code.

**C# (.NET 10 LTS, ASP.NET Core):** nullable reference types enabled; warnings as errors;
EF Core for transactional data (Dapper only with profiling evidence); gRPC internal, REST external;
OIDC/OAuth 2.0 for identity.

**Python (3.12–3.14 pinned):** FastAPI for management/batch APIs, gRPC for streaming; `ruff` and
`mypy` clean; pytest; provider abstractions per master prompt §13.

**TypeScript (web, Phase 3+):** strict mode; API clients generated from contracts, never
hand-written against endpoints.

**Contracts:** protobuf packages are versioned (`voxmesh.<domain>.v1`); breaking changes require a
new version or an explicit migration strategy, enforced by `buf breaking` in CI.

## Git workflow

Trunk-based GitHub Flow (ADR-0017). Branch `<type>/<issue>-<slug>` from `main`; short-lived;
squash-merged via PR; `main` always releasable; feature flags for incomplete behavior. Details in
`CONTRIBUTING.md`; required protections in `docs/development/branch-protection.md`.

## AI task specification

Every delegated AI coding task must specify:

```text
Issue:
Objective:
Context:
Allowed files:
Forbidden files:
Acceptance criteria:
Required tests:
Performance constraints:
Security constraints:
Compatibility constraints:
Commands to run:
Expected output:
```

The `.github/ISSUE_TEMPLATE/ai_task.yml` template mirrors this.

## Parallel-agent rules

- One agent, one GitHub issue, one short-lived branch; prefer separate git worktrees locally.
- Assign an integration owner before two agents touch high-contention files; never let two agents
  modify shared contracts concurrently. Merge contract changes first, then rebase dependents.
- Give every agent explicit scope, acceptance criteria, allowed/forbidden files, and test commands.
  Never ask an agent to "improve the whole repository".
- Nothing merges without review by a human or an explicitly assigned reviewing agent.

## Tool responsibilities (defaults, not restrictions)

- **Claude Code** — architecture, ADRs, repo-wide analysis, complex C++ design, cross-module
  implementation, contract design, security review, refactoring plans.
- **Cursor** — interactive implementation, debugging, small refactors, navigation, local test
  runs, UI development.
- **OpenAI Codex** — isolated issues, unit tests, static-analysis remediation, docs updates,
  small independent features, PR review, parallel platform-specific tasks.
