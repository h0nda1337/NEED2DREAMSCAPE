param(
    [string]$OutputName = 'NEED2DREAMSCAPE_compilado.exe',
    [switch]$IfAvailable
)

$ErrorActionPreference = 'Stop'
$candidates = @((Get-Command gcc.exe -All -ErrorAction SilentlyContinue | ForEach-Object Source))
if ($env:LOCALAPPDATA) {
    $candidates += Join-Path $env:LOCALAPPDATA 'msys64\ucrt64\bin\gcc.exe'
}
if ($env:SystemDrive) {
    $candidates += Join-Path $env:SystemDrive 'msys64\ucrt64\bin\gcc.exe'
    $candidates += Join-Path $env:SystemDrive 'msys64\mingw64\bin\gcc.exe'
}

$compiler = $null
foreach ($candidate in $candidates) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
    $prefix = Split-Path (Split-Path $candidate)
    $includePath = Join-Path $prefix 'include'
    $libPath = Join-Path $prefix 'lib'
    $zlib = Join-Path $libPath 'libz.a'
    if ((Test-Path -LiteralPath (Join-Path $includePath 'SDL2\SDL.h')) -and
        (Test-Path -LiteralPath (Join-Path $libPath 'libSDL2.dll.a')) -and
        (Test-Path -LiteralPath $zlib)) {
        $compiler = $candidate
        break
    }
}

if (-not $compiler) {
    if (-not $IfAvailable) {
        Write-Host 'Para compilar se necesita GCC para Windows con los paquetes SDL2 y zlib.'
    }
    exit 2
}

Push-Location -LiteralPath $PSScriptRoot
try {
    & $compiler '-std=c89' '-pedantic-errors' '-Wall' '-Wextra' '-Wshadow' '-Wconversion' '-Werror' '-O2' `
        '-I' $includePath 'src/engine.c' 'src/game.c' 'src/json.c' 'src/lighting.c' 'src/physics.c' 'src/localization.c' `
        '-L' $libPath '-lSDL2' '-lopengl32' $zlib '-lshell32' '-lole32' '-luuid' '-luser32' '-lm' '-o' $OutputName
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
