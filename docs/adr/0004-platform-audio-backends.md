# 0004 — Platform-specific audio backends

**Status:** Accepted
**Date:** 2026-07-11

## Context

The recorder must capture microphone and system/loopback audio on Windows, macOS, and Linux.
Each OS has exactly one well-supported modern path; the common core must stay platform-free
(ports-and-adapters, master prompt §6).

## Decision

Platform-neutral interfaces (`IAudioCaptureBackend`, `IAudioDeviceEnumerator`, `IAudioStream`, …)
live in `libs/cpp/audio-core`; implementations live under `platform/`:

- **Windows** (`platform/windows/audio-wasapi`): WASAPI microphone capture; shared-mode
  event-driven loopback; process loopback where supported; device-notification APIs; explicit
  default-device-change and disconnection recovery.
- **macOS** (`platform/macos/audio-screencapturekit`): ScreenCaptureKit for system/application
  audio; Core Audio/Audio Units for microphone; Objective-C++ bridges isolated in the adapter —
  no Objective-C types in common APIs; explicit permission flows and entitlements.
- **Linux** (`platform/linux/audio-pipewire`): PipeWire for mic sources and output monitors;
  PipeWire device discovery; session-manager-aware; clear diagnostics when a source/monitor is
  unavailable. PulseAudio is **not** the primary implementation; a compatibility fallback needs
  its own ADR.

Backends are selected by factory at startup; a fake backend ships for tests (Phase 1).

## Alternatives considered

- **Portable audio layers (PortAudio, miniaudio, RtAudio):** loopback/system-capture support is
  incomplete or bolted on exactly where this product lives; direct control over timestamps,
  device change, and process loopback is required.
- **PulseAudio on Linux:** legacy; PipeWire is the current standard on all target Ubuntu versions.

## Consequences

Three backend codebases with one contract; integration tests need real-audio runners per OS;
core logic (buffers, drift, sequencing) is written and tested once.

## Security implications

macOS capture requires screen/audio permission (TCC) and entitlements; Windows process loopback
touches other processes' audio — capture-exclusion rules and consent (master prompt §18) apply in
the core, not per backend.

## Operational implications

Per-OS CI runners with audio capability for integration tests (self-hosted where GitHub-hosted
lacks devices).

## Migration or rollback plan

Interfaces isolate replacements (e.g. adding a PulseAudio fallback or a new Windows API) to a new
adapter plus factory registration.

## References

Master prompt §6. ADR-0002.
