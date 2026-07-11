# License inventory

VoxMesh is proprietary software. No open-source license is granted for this repository unless
and until one is added here explicitly.

This directory tracks the license inventory for third-party dependencies, as required by the
C++ quality standards (master prompt §26) and the Qt licensing ADR (ADR-0003).

## Third-party dependencies

| Dependency | License | Where used | Notes |
| --- | --- | --- | --- |
| GoogleTest | BSD-3-Clause | C++ tests | via Conan |
| nlohmann/json | MIT | audio-core config parsing | via Conan |
| Google Benchmark | Apache-2.0 | C++ benchmarks | via Conan |
| Qt 6 | LGPL-3.0 / Commercial | Desktop recorder | **Pending legal review — see ADR-0003** |

Add a row whenever a dependency is introduced. SBOM generation in CI supplements, but does not
replace, this inventory. Legal conclusions (e.g., LGPL compliance strategy) belong to legal
review, not to this file.
