# 0005 — CMake and Conan dependency strategy

**Status:** Accepted
**Date:** 2026-07-11

## Context

C++ builds must be reproducible across three OSes and CI, with pinned third-party dependencies
(GoogleTest now; protobuf/gRPC, FFmpeg, WebRTC APM later) and multiple configurations
(debug/release/sanitizers/coverage).

## Decision

CMake ≥ 3.28 with committed `CMakePresets.json` covering the configurations in master prompt §26;
out-of-source builds under `build/`; `CMakeUserPresets.json` is gitignored and never committed.
Conan 2 manages all third-party C++ dependencies with exact pinned versions
(`conanfile.py` at the repo root); Conan generates the CMake toolchain consumed by our presets via
`CMAKE_TOOLCHAIN_FILE`. No floating versions; no vendored source drops; no FetchContent for
third-party code.

## Alternatives considered

- **vcpkg:** viable; Conan 2's profiles and binary management fit the multi-config,
  cross-platform matrix better, and the spec mandates Conan.
- **FetchContent/submodules:** no binary caching, license/SBOM tracking is manual, build times
  grow unboundedly.

## Consequences

One documented flow (`conan install` → `cmake --preset` → `ctest --preset`) for every platform;
CI caches the Conan package folder; new deps require a pin, a license inventory row, and pass
through vulnerability scanning.

## Security implications

Pinned versions + Conan revisions give a verifiable dependency graph for SBOM generation;
conancenter packages build from source (`--build=missing`) when prebuilt binaries are absent,
reducing exposure to tampered binaries.

## Operational implications

Compiler updates (e.g. new MSVC) may force local builds of dependencies until conancenter catches
up — acceptable CI-time cost.

## Migration or rollback plan

Conan is isolated to dependency provisioning; CMake targets consume standard imported targets, so
switching to vcpkg would touch provisioning scripts and presets only.

## References

Master prompt §3, §26. `conanfile.py`, `CMakePresets.json`.
