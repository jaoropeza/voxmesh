# 0006 — Separate microphone and speaker tracks

**Status:** Accepted
**Date:** 2026-07-11

## Context

Downstream AI (AEC, diarization, STT accuracy, reprocessing) depends on what was captured, not on
what was convenient to store. A mixed track destroys information permanently: echo cancellation
needs the far-end reference; local-vs-remote speaker separation needs distinct tracks.

## Decision

Capture and preserve separate synchronized tracks (master prompt §7): (1) original microphone,
(2) original speaker/system output, (3) echo-cancelled microphone when enabled, (4) optional
derived mix, (5) derived STT stream. Archival at 48 kHz float32/pcm_s16le, source channel layout
preserved; STT stream at 16 kHz mono pcm_s16le, 100 ms frames. Container preferences: Matroska
for multi-track, FLAC for lossless single tracks, WAV for diagnostics/fixtures, Opus for derived
bandwidth-efficient audio. Original tracks are never discarded because a mix exists.

## Alternatives considered

- **Single mixed track:** halves storage, permanently loses the AEC reference and speaker
  separation; reprocessing with better future models becomes impossible.
- **Mix + mic only:** still loses clean far-end reference.

## Consequences

Roughly 2–3× raw storage versus a single mix (FLAC mitigates); every track carries its own
sequence numbers and timestamps (ADR-0007); retention policies can expire derived tracks earlier
than originals.

## Security implications

More sensitive artifacts to protect per meeting; all tracks inherit the same encryption,
retention, and legal-hold treatment.

## Operational implications

Storage sizing and lifecycle rules must be per-track-class; export tooling must select tracks
explicitly.

## Migration or rollback plan

Derived tracks can always be regenerated from originals; the reverse is impossible — this is the
one-way door the decision avoids.

## References

Master prompt §7, §10. ADR-0007, ADR-0012.
