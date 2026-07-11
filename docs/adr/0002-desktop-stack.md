# 0002 — C++20 and Qt 6 desktop stack

**Status:** Accepted
**Date:** 2026-07-11

## Context

The desktop recorder needs low-latency native audio capture (WASAPI, ScreenCaptureKit/Core Audio,
PipeWire), real-time-safe processing, and a cross-platform UI on Windows 10/11 x64, macOS
(x86_64 + arm64), and Ubuntu 22.04/24.04/26.04 x64.

## Decision

C++20 for the recorder core and platform backends; Qt 6 with QML for the desktop UI; GoogleTest/
GoogleMock for tests; gRPC/protobuf for transport; FFmpeg libraries for encoding/muxing; WebRTC
Audio Processing Module (behind an interface) for AEC. Exact versions pinned via Conan (ADR-0005).

## Alternatives considered

- **Electron/Tauri + native audio helper:** splits the real-time path across a process boundary
  and balloons memory; weak fit for professional capture reliability.
- **Rust core:** attractive safety story, but the audio SDK ecosystem (Qt bindings, WebRTC APM,
  FFmpeg integration) and team familiarity favor C++ today.
- **Per-platform native apps (WinUI/SwiftUI/GTK):** triples UI effort before product-market fit.

## Consequences

One capture core and one UI codebase across three OSes; requires real-time C++ discipline
(master prompt §8) and sanitizer-heavy CI; Objective-C++ and platform code stay isolated under
`platform/`.

## Security implications

C++ memory safety risk mitigated by RAII rules, warnings-as-errors, clang-tidy, ASan/UBSan/TSan
in CI, and fuzzing of media parsers in later phases.

## Operational implications

Qt runtime deployment in installers; crash reporting and symbol management needed per platform.

## Migration or rollback plan

UI layer is replaceable independently of the audio core (ports-and-adapters); core libraries are
plain C++ with no Qt dependency, preserving optionality.

## References

Master prompt §2, §3, §6, §8. ADR-0003 (licensing), ADR-0004 (backends).
