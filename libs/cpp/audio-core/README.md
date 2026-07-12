# voxmesh::audio-core

Platform-neutral audio core (master prompt §6, §8, §31 Phase 1; ADR-0004, ADR-0006, ADR-0007).
No platform APIs are included here — WASAPI/ScreenCaptureKit/PipeWire backends implement these
interfaces under `platform/` from Phase 2 onward.

| Header | Purpose |
| --- | --- |
| `types.hpp` | Strong types: `SampleRate`, `ChannelCount`, `SequenceNumber`, `MonotonicTimestamp`, `SampleFormat`, `TrackKind` |
| `frame.hpp` | `AudioFrame` — the in-memory counterpart of `voxmesh.media.v1.AudioFrame` |
| `ring_buffer.hpp` | `SpscRingBuffer<T>` — the bounded lock-free queue mandated on the capture path |
| `capture.hpp` | `IAudioCaptureBackend`, `IAudioDeviceEnumerator`, `IAudioCaptureStream`, `IAudioFrameSink` |
| `clock_sync.hpp` | `IClockSynchronizer` / `TrackSynchronizer` — master timeline, drift ppm, explicit silence for gaps (ADR-0007) |
| `session.hpp` | `RecordingSession` state machine (Idle/Recording/Paused/Stopping/Stopped/Failed) |
| `config.hpp` | `RecorderConfig` + JSON loader; defaults implement spec §7 |
| `testing/signal_generator.hpp` | Deterministic PCM source (silence/ramp/sine) for tests and fixtures |
| `testing/fake_backend.hpp` | Thread-free capture backend with loss/restart fault injection |

Interfaces not yet defined (`IAudioResampler`, `IAudioProcessor`, `IAudioEncoder`,
`IRecordingWriter`, `IStreamingAudioClient`, `IConsentService`) arrive with the phases that first
implement them. Clock sync detects and reports drift; gradual resampling/sample-slip *correction*
of that drift arrives with the resampler.

Benchmarks (`benchmarks/`) build with the default configuration and run manually:

```sh
build/<preset>/libs/cpp/audio-core/benchmarks/voxmesh-audio-core-bench
```

Real-time rules apply to everything on the capture path: see AGENTS.md and master prompt §8.
