# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Read [AGENTS.md](AGENTS.md) first** — it is the primary development contract (ground rules,
architecture invariants, language standards, git workflow, AI task spec). This file extends it
with Claude-specific guidance and does not repeat it. The full product specification is
[docs/architecture/voxmesh-master-prompt.md](docs/architecture/voxmesh-master-prompt.md); decisions
live in [docs/adr/](docs/adr/) — check both before architectural work.

## What this is

VoxMesh: enterprise meeting recording, transcription, translation, diarization, summarization, and
semantic-search platform. Monorepo: C++20/Qt6 desktop recorder (`apps/desktop-recorder`, shared
libs in `libs/cpp`, platform audio backends in `platform/`), ASP.NET Core control plane
(`services/control-plane`), Python AI services (`services/ai/*`), versioned protobuf contracts
(`libs/contracts`), Next.js web app later (`apps/web`).

Delivery phases (master prompt §31): 0 foundation → 1 platform-neutral audio core → 2 Windows
vertical slice (first MVP) → 3 backend → 4 macOS → 5 Ubuntu → 6 AI post-processing → 7 provider
adapters. **Phases 0 and 1 are implemented (audio core in `libs/cpp/audio-core` — see its README);
Phase 2 (Windows WASAPI vertical slice) is next and needs a local C++ toolchain + Qt.**

## Claude Code's role here

Per AGENTS.md tool responsibilities, prefer using Claude Code for: architecture and ADRs,
repo-wide analysis, complex C++ design (especially real-time audio paths), cross-module changes,
contract design, and security review. Present a plan before large implementations. One issue, one
branch (`<type>/<issue>-<slug>`), commits stay small and reviewable.

## Build and test commands

Full details in [README.md](README.md). Quick reference:

```sh
# C++ (needs CMake ≥3.28, Conan 2, C++20 compiler; Qt optional — app target skips without it)
conan install . --output-folder build/conan --build=missing -s build_type=Debug
cmake --preset windows-debug && cmake --build --preset windows-debug
ctest --preset windows-debug                    # all C++ tests
ctest --preset windows-debug -R RingBufferTest  # single test filter (VersionTest, SessionTest, ...)

# Control plane (.NET 10)
dotnet test services/control-plane              # all tests
dotnet test services/control-plane --filter FullyQualifiedName~HealthEndpoint  # single test

# Python AI services (per-service venv)
cd services/ai/stt && pip install -e .[dev] -c constraints.txt
pytest                                          # all tests;  pytest -k health  for one
ruff check . && mypy src

# Contracts
npx @bufbuild/buf lint libs/contracts/proto
npx @bufbuild/buf breaking libs/contracts/proto --against '.git#branch=main,subdir=libs/contracts/proto'
```

Local environment note: this machine has .NET 10 SDK, Python 3.14, and Node 24, but **no C++
toolchain** (VS 2026 is installed without the C++ workload — no MSVC/CMake/Ninja) and no Qt.
C++ changes are validated by CI until that is installed.

## Repo-specific gotchas

- `CMakeUserPresets.json` is intentionally gitignored — never commit it (spec §26).
- The desktop app target is gated on `find_package(Qt6 QUIET)`; CI sets `VOXMESH_REQUIRE_QT=ON`
  so a broken Qt setup fails loudly there instead of silently skipping.
- Proto packages are `voxmesh.<domain>.v1` — breaking changes need a new version, and
  `buf breaking` in CI enforces this against `main`.
- ADR-0003 (Qt licensing) is *Proposed*, pending legal review — flag, don't conclude, licensing
  questions in code or docs.
