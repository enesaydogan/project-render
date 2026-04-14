param(
    [string]$Configuration = 'Release'
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$buildDir = Join-Path $scriptDir 'build'
$previousTlsVerify = $env:CMAKE_TLS_VERIFY
$cmakeArgs = @(
    '-S', $scriptDir,
    '-B', $buildDir,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    '-DUSE_TINYGLTF=ON',
    '-DUSE_QT_UI=ON'
)

$qtDirCandidates = @(
    'D:\Qt\6.10.2\msvc2022_64\lib\cmake\Qt6'
)

$detectedQtDir = $null
foreach ($candidate in $qtDirCandidates) {
    if (Test-Path $candidate) {
        $detectedQtDir = $candidate
        break
    }
}

if (-not $detectedQtDir) {
    $detectedQtDir = Get-ChildItem 'D:\Qt' -Directory -Recurse -Filter msvc2022_64 -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'lib\cmake\Qt6' } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
}

if ($detectedQtDir) {
    $cmakeArgs += "-DQt6_DIR=$detectedQtDir"
    Write-Host "Using Qt6 from: $detectedQtDir"
} else {
    Write-Warning "Qt6 not found under D:\Qt. CMake will disable USE_QT_UI if it cannot locate Qt on its own."
}

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

try {
    # Some dependency downloads fail on Windows machines with strict or broken
    # certificate/revocation checks; let CMake proceed in that case.
    $env:CMAKE_TLS_VERIFY = '0'

    Write-Host "Configuring (Visual Studio 17 2022)..."
    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "Building ($Configuration)..."
    cmake --build $buildDir --config $Configuration -j 8
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "Build complete. Output in: $buildDir"
}
finally {
    if ($null -eq $previousTlsVerify -or $previousTlsVerify -eq '') {
        Remove-Item Env:CMAKE_TLS_VERIFY -ErrorAction SilentlyContinue
    } else {
        $env:CMAKE_TLS_VERIFY = $previousTlsVerify
    }
}

#Start-Process .\build\Release\project-render.exe ".\assets\DamagedHelmet.glb"
