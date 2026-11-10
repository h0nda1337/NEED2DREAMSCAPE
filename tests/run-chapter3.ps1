$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot
$compiler = (Get-Command gcc -ErrorAction Stop).Source
$prefix = Split-Path (Split-Path $compiler)
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('n2d-c3-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $temporary 'work') -Force | Out-Null
$binary = Join-Path $temporary 'chapter3.exe'
$sources = @((Join-Path $PSScriptRoot 'chapter3.c'))
$sources += 'game.c','json.c','lighting.c','physics.c','localization.c' | ForEach-Object { Join-Path $repository "game/src/$_" }
& $compiler '-std=c89' '-O2' '-I' (Join-Path $prefix 'include') @sources '-L' (Join-Path $prefix 'lib') '-lSDL2' '-lopengl32' (Join-Path $prefix 'lib/libz.a') '-lshell32' '-lole32' '-luuid' '-luser32' '-lm' '-o' $binary
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de las pruebas.' }
Copy-Item -LiteralPath (Join-Path $repository 'game/SDL2.dll') -Destination $temporary
Push-Location $temporary
try {
    & $binary (Join-Path $repository 'game/assets')
    if ($LASTEXITCODE -ne 0) { throw 'Falló el recorrido del capítulo 3.' }
} finally { Pop-Location }
