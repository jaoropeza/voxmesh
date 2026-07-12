# Windows installer (MSI)

WiX-authored MSI for the VoxMesh Recorder (master prompt §29, ADR-0018). Built by
[build-msi.ps1](build-msi.ps1); produced automatically by the `package-windows` CI job, which
uploads it as a build artifact and smoke-tests silent install/uninstall on every run.

**Signing:** the MSI is unsigned until the protected `code-signing` environment exists
(ADR-0018). Do not distribute unsigned builds outside development.

**WiX version:** pinned to **v6.0.2** (MS-RL). WiX v7+ requires accepting the Open Source
Maintenance Fee EULA for commercial use — flagged for the same legal review as ADR-0003; do
not upgrade the pin before that concludes. The WiX license covers the build tool only and
does not extend to the produced MSI.

## Install behavior

- Per-machine install to `%ProgramFiles%\VoxMesh Recorder` with a Start-menu shortcut.
- The Qt runtime, QML modules, and VC++ runtime are deployed app-locally — no prerequisites.
- Recordings are written to the user's `Music\VoxMesh` folder at runtime (not touched by
  install or uninstall).

## Silent install / upgrade / uninstall (§29)

```powershell
# Install (or upgrade in place — MajorUpgrade replaces any older version)
msiexec /i voxmesh-recorder-<version>-x64.msi /qn

# Custom folder
msiexec /i voxmesh-recorder-<version>-x64.msi /qn INSTALLFOLDER="D:\Apps\VoxMesh"

# Uninstall
msiexec /x voxmesh-recorder-<version>-x64.msi /qn

# Logged install for diagnostics
msiexec /i voxmesh-recorder-<version>-x64.msi /qn /l*v install.log
```

- **Upgrade:** newer version installs over older (files replaced, shortcut kept). The
  `UpgradeCode` in [Package.wxs](Package.wxs) is permanent; changing it breaks upgrades.
- **Downgrade:** refused with an error message; uninstall first (§29 rollback path).
- **Uninstall:** removes all installed files and the shortcut; user data (recordings,
  settings) is retained.

## Building locally

```powershell
dotnet tool install --global wix --version 6.0.2       # once
cmake --build --preset windows-release
pwsh packaging/windows/build-msi.ps1                   # Release MSI
pwsh packaging/windows/build-msi.ps1 -Config Debug     # structure iteration only (debug
                                                       # VC runtime is not redistributable)
```

Verify payload without installing:

```powershell
msiexec /a build\packaging\windows\release\voxmesh-recorder-<version>-x64.msi /qn TARGETDIR=C:\temp\vx-extract
```
