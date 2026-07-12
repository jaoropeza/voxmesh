# Builds the VoxMesh Recorder MSI (issue #16, ADR-0018).
#
# Prerequisites: the app is already built for the chosen config
# (cmake --build --preset windows-<config>), Qt's windeployqt is available,
# and the WiX CLI is installed (dotnet tool install --global wix).
#
# Usage:
#   pwsh packaging/windows/build-msi.ps1                        # Release
#   pwsh packaging/windows/build-msi.ps1 -Config Debug          # local iteration only
#   pwsh packaging/windows/build-msi.ps1 -QtBinDir C:/Qt/6.11.1/msvc2022_64/bin

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$QtBinDir = "C:/Qt/6.11.1/msvc2022_64/bin"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$presetDir = Join-Path $repoRoot ("build/windows-" + $Config.ToLower())
$exePath = Join-Path $presetDir "apps/desktop-recorder/$Config/voxmesh-recorder.exe"
if (-not (Test-Path $exePath)) {
    throw "App not built: $exePath missing. Run: cmake --build --preset windows-$($Config.ToLower())"
}

$windeployqt = Join-Path $QtBinDir "windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found at $windeployqt (pass -QtBinDir)."
}

# Version comes from the root CMake project() declaration.
$cmakeLists = Get-Content (Join-Path $repoRoot "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') {
    throw "Could not parse project version from CMakeLists.txt"
}
$version = $Matches[1]

$packagingDir = Join-Path $repoRoot "build/packaging/windows/$($Config.ToLower())"
$stagingDir = Join-Path $packagingDir "staging"
if (Test-Path $stagingDir) { Remove-Item -Recurse -Force $stagingDir }
New-Item -ItemType Directory -Force $stagingDir | Out-Null

Copy-Item $exePath $stagingDir
# --compiler-runtime app-locally deploys the VC++ runtime so the MSI has no
# external prerequisite (release builds only; debug runtimes are not
# redistributable and only support local iteration).
& $windeployqt --qmldir (Join-Path $repoRoot "apps/desktop-recorder/qml") `
    --compiler-runtime --no-translations `
    (Join-Path $stagingDir "voxmesh-recorder.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

$msiPath = Join-Path $packagingDir "voxmesh-recorder-$version-x64.msi"
wix build (Join-Path $PSScriptRoot "Package.wxs") `
    -arch x64 `
    -d StagingDir=$stagingDir `
    -d ProductVersion=$version `
    -o $msiPath
if ($LASTEXITCODE -ne 0) { throw "wix build failed with exit code $LASTEXITCODE" }

Write-Host "MSI: $msiPath"
