# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

This is a **greenfield repository**. There is no code, build system, or git history yet. The
authoritative specification for everything to be built is
`docs/architecture/voxmesh-master-prompt.md` — **read it before doing any implementation work.**
This file summarizes it; the master prompt wins on any conflict.

The spec calls for a tool-neutral `AGENTS.md` as the primary development contract, with this file
referencing and extending it. `AGENTS.md` does not exist yet; creating it is part of Phase 0.

## What VoxMesh is

An enterprise, cross-platform meeting recording, transcription, translation, diarization,
summarization, and semantic-search platform. Monorepo containing:

- **Desktop recorder** — C++20, Qt 6/QML, CMake + CMake Presets, Conan 2, GoogleTest, gRPC/protobuf,
  FFmpeg. Targets Windows 10/11 x64, macOS (Intel + Apple Silicon), Ubuntu 22.04/24.04/26.04 x64.
- **Web app** — TypeScript, React, Next.js; typed API client generated from OpenAPI/protobuf contracts.
- **Control plane** — C#/.NET LTS, ASP.NET Core, EF Core, gRPC internal + REST external, OIDC/OAuth 2.0.
- **AI services** — Python, FastAPI (management/batch) + gRPC (streaming), provider abstractions for
  STT, translation, diarization, summarization, embeddings.
- **Infrastructure** — Kubernetes, Kafka, PostgreSQL (source of truth), Redis (ephemeral only, never
  authoritative), S3-compatible storage, Qdrant behind an `IVectorStore` interface, GitHub Actions,
  OpenTelemetry.

The intended repository layout is in master prompt §19 (`apps/`, `libs/`, `platform/`, `services/`,
`infra/`, `packaging/`, `tests/`, `docs/`).

## Load-bearing architecture rules

These require reading multiple spec sections to piece together; violating them is expensive to undo:

- **Four planes** (§5): desktop/provider ingress, real-time media, control plane, async AI/indexing.
  Provider-specific schemas (Teams/Meet/Zoom) must not leak past their adapters.
- **Latency-critical audio path bypasses Kafka** (§5.2): capture adapter → gRPC → media gateway →
  streaming STT → live transcript service → WebSocket/SSE. Kafka is for durable events and async
  downstream workflows only.
- **Ports-and-adapters audio core** (§6): the common C++ audio core must not include Windows
  (WASAPI), macOS (ScreenCaptureKit/Core Audio — no Objective-C++ leaking into common APIs), or
  Linux (PipeWire, not PulseAudio) platform APIs. Platform backends live under `platform/`.
- **Real-time callback constraints** (§8): audio callbacks may only read the frame, attach
  sequence/timestamp metadata, push into a preallocated bounded ring buffer, and bump lock-free
  counters. No I/O, logging, heap allocation, blocking locks, or unbounded queues. Every dropped
  frame must emit a metric and diagnostic event. No owning raw pointers, no detached threads, no
  exceptions crossing C APIs or thread boundaries.
- **Separate synchronized tracks** (§7, §9): original mic, original speaker output, echo-cancelled
  mic, optional mix, and derived STT stream are distinct tracks. Never discard originals because a
  mix exists. Never align tracks by callback arrival order — use monotonic timestamps, per-track
  sequence numbers, drift detection, and explicit discontinuity metadata. Archival: 48 kHz;
  STT: 16 kHz mono pcm_s16le, 100 ms frames. Matroska/FLAC preferred.
- **Transcript segments are revisioned** (§12): partial results replace the mutable revision of the
  same segment until finalized — never appended as independent final records. All protobuf contracts
  are versioned.
- **Provider abstraction everywhere** (§13): business logic never sees model-specific classes or
  hard-coded model names. Same for the vector store.
- **Originals are sacred** (§14, §15): translations never replace source transcripts; inferred
  diarization labels are never silently promoted to confirmed participant identity.
- **Provenance** (§16): every extracted decision/action item carries evidence segment IDs.
- **Search authorization is enforced at retrieval time** (§17), not by post-filtering results.
- **Privacy defaults** (§11, §30): no audio, transcript text, participant names, or tokens in logs
  or metrics; local spool is encrypted.

## Workflow rules

- Work in **small, testable vertical slices** — never attempt the whole platform in one change.
- Before modifying code: inspect the repo, read `AGENTS.md`/ADRs/task specs, identify the issue and
  acceptance criteria, present a concise plan, record material decisions as ADRs (`docs/adr/`,
  format in §25), then implement.
- Ask questions only when a missing decision blocks correct implementation; otherwise pick the
  safest reasonable default and document the assumption.
- **Git**: trunk-based GitHub Flow (no GitFlow). Branch names include the GitHub issue number
  (e.g. `feat/123-wasapi-loopback`). `main` is always releasable; squash merges; short-lived
  branches; feature flags for incomplete behavior.
- Pin exact dependency versions; never depend on floating versions like `latest`.
- Do not commit `CMakeUserPresets.json`.
- PRs should stay under ~500 changed lines (excluding generated/lock files) and follow the
  requirements in §23.

## Build and test commands

None exist yet. Phase 0 (§31) establishes the CMake project, CMake Presets, Conan config, minimal
Qt app, minimal ASP.NET Core service, minimal Python service, protobuf contract package, and CI
matrix. **When the build system lands, update this section** with the concrete commands (configure
preset, build, run a single test, lint).

Delivery order (§31): Phase 0 foundation → Phase 1 platform-neutral audio core (must build on all
three OSes before any native backend) → Phase 2 Windows vertical slice (first end-to-end MVP) →
Phase 3 backend → Phase 4 macOS → Phase 5 Ubuntu → Phase 6 AI post-processing → Phase 7 meeting
provider adapters.
