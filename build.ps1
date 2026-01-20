param(
    [string]$Configuration = 'Release'
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$buildDir = Join-Path $scriptDir 'build'

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

Write-Host "Configuring (Visual Studio 17 2022)..."
cmake -S $scriptDir -B $buildDir -G "Visual Studio 17 2022"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building ($Configuration)..."
cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build complete. Output in: $buildDir"

Start-Process -FilePath .\build\Release\project-render.exe
