# 0008 — gRPC media transport

**Status:** Accepted
**Date:** 2026-07-11

## Context

The desktop recorder streams audio frames to the backend with near-real-time STT partials coming
back. The path needs backpressure, multiplexing, TLS, typed contracts, and C++/C#/Python support.

## Decision

The latency-critical path is a direct bidirectional gRPC stream: capture adapter → media gateway →
streaming STT worker → live transcript service → WebSocket/SSE to clients. Contracts are protobuf
(`voxmesh.media.v1.AudioFrame`, master prompt §12). Kafka is explicitly **not** on this path
(ADR-0009).

## Alternatives considered

- **WebRTC media channels:** excellent for lossy conversational audio; wrong fit for lossless
  archival frames with app-level sequencing, and heavy operationally (SFU, ICE).
- **Raw WebSocket + custom framing:** reinvents flow control, typing, and multiplexing gRPC
  already provides.
- **Kafka as transport:** durable but adds broker hops and per-frame latency; rejected for the
  hot path.

## Consequences

HTTP/2 end-to-end through load balancers must be validated; client reconnect/replay logic keys on
`sequence` + local spool (ADR-0016); the media gateway owns session admission and fan-out.

## Security implications

mTLS or token-authenticated channels; per-stream authorization at session admission; frame
payloads are sensitive and never logged.

## Operational implications

gRPC keepalive/deadline tuning per platform; gateway horizontal scaling with session affinity.

## Migration or rollback plan

`IStreamingAudioClient` abstracts the transport on the client; a transport swap would not touch
capture or persistence code.

## References

Master prompt §5.2, §12. ADR-0009.
