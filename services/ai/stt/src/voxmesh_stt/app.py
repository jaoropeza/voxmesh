"""FastAPI management API for the STT service.

Phase 0 skeleton: health and version only. Streaming recognition arrives with
the gRPC server in Phase 3, behind the IStreamingSpeechToTextProvider
abstraction (master prompt §13) — model access never lives in route handlers.
"""

from fastapi import FastAPI

from voxmesh_stt import __version__

app = FastAPI(title="VoxMesh STT Service", version=__version__)


@app.get("/healthz")
def healthz() -> dict[str, str]:
    """Liveness/readiness probe for Kubernetes and CI smoke tests."""
    return {"status": "ok", "service": "stt", "version": __version__}
