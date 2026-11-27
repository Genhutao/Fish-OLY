# OLY Persona — Windows build (PowerShell)
# Usage:  pwsh -File .\build.ps1            # build bin/oly-persona.exe
#         pwsh -File .\build.ps1 -Target linux
#
# Uses a project-local Go toolchain if present (..\.gotool\go), otherwise `go`
# from PATH. Frontend is built with pnpm when available, else Vite directly.
param(
    [ValidateSet('windows', 'linux')]
    [string]$Target = 'windows'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$proj = $PSScriptRoot

$go = Join-Path $root '.gotool\go\bin\go.exe'
if (-not (Test-Path $go)) { $go = (Get-Command go -ErrorAction SilentlyContinue).Source }
if (-not $go) { throw 'Go toolchain not found (set GOROOT or install Go)' }

if (Test-Path (Join-Path $root '.gotool\go')) { $env:GOROOT = Join-Path $root '.gotool\go' }
$env:GOPATH = if ($env:GOPATH) { $env:GOPATH } else { Join-Path $root '.gopath' }
$env:GOMODCACHE = if ($env:GOMODCACHE) { $env:GOMODCACHE } else { Join-Path $root '.gomodcache' }
$env:GOCACHE = if ($env:GOCACHE) { $env:GOCACHE } else { Join-Path $root '.gocache' }
$env:CGO_ENABLED = '0'

Write-Host '==> Building frontend…'
Push-Location (Join-Path $proj 'frontend')
try {
    $pnpm = Join-Path (Split-Path (Get-Command node).Source) 'pnpm.cmd'
    if (Test-Path $pnpm) {
        & $pnpm build
    } else {
        & node 'node_modules/vite/bin/vite.js' build
    }
    if ($LASTEXITCODE -ne 0) { throw 'frontend build failed' }
} finally { Pop-Location }

Write-Host "==> Building $Target/amd64…"
$ldflags = '-w -s'
$out = 'bin/oly-persona'
if ($Target -eq 'windows') {
    $ldflags += ' -H windowsgui'
    $out += '.exe'
}
$env:GOOS = $Target
$env:GOARCH = 'amd64'
try {
    & $go build -tags desktop,production -trimpath -ldflags $ldflags -o $out .
    if ($LASTEXITCODE -ne 0) { throw 'go build failed' }
} finally {
    Remove-Item Env:GOOS -ErrorAction SilentlyContinue
    Remove-Item Env:GOARCH -ErrorAction SilentlyContinue
}
Write-Host "==> Done: $out"
