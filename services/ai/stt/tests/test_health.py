from fastapi.testclient import TestClient

from voxmesh_stt import __version__
from voxmesh_stt.app import app

client = TestClient(app)


def test_healthz_reports_ok() -> None:
    response = client.get("/healthz")

    assert response.status_code == 200
    assert response.json() == {"status": "ok", "service": "stt", "version": __version__}


def test_version_matches_package_metadata() -> None:
    assert app.version == __version__
