# VoxMesh

Enterprise, cross-platform meeting recording, transcription, translation, diarization,
summarization, and semantic-search platform.

**Status: Phase 0 — repository and architecture foundation.** No production audio capture exists
yet. The authoritative product and architecture specification is
[docs/architecture/voxmesh-master-prompt.md](docs/architecture/voxmesh-master-prompt.md); material
decisions are recorded as ADRs in [docs/adr/](docs/adr/).

## Components

| Path | What | Stack |
| --- | --- | --- |
| `apps/desktop-recorder/` | Desktop recording client | C++20, Qt 6/QML |
| `libs/cpp/` | Shared C++ libraries (audio core lands in Phase 1) | C++20, CMake, Conan 2 |
| `libs/contracts/` | Versioned protobuf/OpenAPI contracts | Protocol Buffers, buf |
| `services/control-plane/` | Tenants, users, meetings, policies | C#, ASP.NET Core (.NET 10 LTS) |
| `services/ai/` | STT, translation, diarization, summarization, embeddings | Python, FastAPI, gRPC |
| `apps/web/` | Web application (Phase 3+) | TypeScript, Next.js |
| `infra/`, `packaging/` | Deployment and installers (later phases) | K8s, Helm, MSI/PKG/DEB |

## Building

### C++ (desktop recorder and libraries)

Requires CMake ≥ 3.28, Conan 2, and a C++20 compiler (MSVC 2022+, Clang, or GCC). Qt 6.5+ is
optional — the desktop app target is skipped when Qt is not found.

```sh
pip install conan
conan profile detect --exist-ok
conan install . --output-folder build/conan --build=missing -s build_type=Debug
cmake --preset windows-debug        # or linux-debug / macos-arm64-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

### Control plane (.NET)

```sh
cd services/control-plane
dotnet build
dotnet test
dotnet run --project src/VoxMesh.ControlPlane.Api
```

### AI services (Python)

```sh
cd services/ai/stt
python -m venv .venv && . .venv/Scripts/activate   # or .venv/bin/activate
pip install -e .[dev] -c constraints.txt
pytest
```

### Contracts

```sh
npx @bufbuild/buf lint libs/contracts/proto
```

## Contributing

Read [AGENTS.md](AGENTS.md) (the development contract, including AI-assisted development rules)
and [CONTRIBUTING.md](CONTRIBUTING.md). Trunk-based development via GitHub Flow; every change
goes through a pull request against `main`.
