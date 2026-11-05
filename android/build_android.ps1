param(
    [string]$SdkPath = $env:ANDROID_SDK_ROOT,
    [string]$NdkPath = $env:ANDROID_NDK_HOME,
    [string]$SDLSource,
    [string]$GL4ESSource,
    [string[]]$Abis = @('arm64-v8a', 'armeabi-v7a')
)
$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$game = Join-Path (Split-Path $project) 'game'
if (-not $SdkPath) { $SdkPath = $env:ANDROID_HOME }
if (-not $SdkPath -or -not (Test-Path -LiteralPath (Join-Path $SdkPath 'platforms/android-35/android.jar'))) {
    throw 'Indica -SdkPath con Android SDK Platform 35 y Build Tools 35.0.0.'
}
if (-not $NdkPath -or -not (Test-Path -LiteralPath (Join-Path $NdkPath 'build/cmake/android.toolchain.cmake'))) {
    throw 'Indica -NdkPath con Android NDK r27c o posterior.'
}

function Get-Dependency([string]$Name, [string]$Url, [string]$Revision, [string]$Existing) {
    if ($Existing) { return (Resolve-Path -LiteralPath $Existing).Path }
    $destination = Join-Path $project ".deps/$Name"
    if (-not (Test-Path -LiteralPath $destination)) {
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        & git -C $destination init --quiet
        & git -C $destination remote add origin $Url
        & git -C $destination fetch --depth 1 origin $Revision
        if ($LASTEXITCODE -ne 0) { throw "No se pudo descargar $Name." }
        & git -C $destination checkout --detach FETCH_HEAD
        if ($LASTEXITCODE -ne 0) { throw "No se pudo preparar $Name." }
    }
    return $destination
}

$sdl = Get-Dependency 'SDL2' 'https://github.com/libsdl-org/SDL.git' 'cee83f2d9a916e9de30c5712e798541fdc134b06' $SDLSource
$gl = Get-Dependency 'gl4es' 'https://github.com/ptitSeb/gl4es.git' 'bc4314d8369f169d3fa81c5ed1b46b3dba4e74a5' $GL4ESSource
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ninja = (Get-Command ninja.exe -ErrorAction Stop).Source
$toolchain = Join-Path $NdkPath 'build/cmake/android.toolchain.cmake'
$strip = Join-Path $NdkPath 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'

foreach ($abi in $Abis) {
    $native = Join-Path $project ".build/native/$abi"
    & $cmake -S (Join-Path $project 'native') -B $native -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DANDROID_ABI=$abi" '-DANDROID_PLATFORM=android-23' `
        '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON' '-DCMAKE_BUILD_TYPE=Release' `
        "-DSDL2_SOURCE=$sdl" "-DGL4ES_SOURCE=$gl"
    if ($LASTEXITCODE -ne 0) { throw "No se pudo configurar $abi." }
    & $cmake --build $native --target main --parallel 6
    if ($LASTEXITCODE -ne 0) { throw "No se pudo compilar $abi." }
    $libraries = Join-Path $project ".build/jniLibs/$abi"
    New-Item -ItemType Directory -Path $libraries -Force | Out-Null
    foreach ($library in @('libmain.so', 'sdl/libSDL2.so')) {
        $target = Join-Path $libraries (Split-Path $library -Leaf)
        Copy-Item -LiteralPath (Join-Path $native $library) -Destination $target -Force
        & $strip --strip-unneeded $target
        if ($LASTEXITCODE -ne 0) { throw "No se pudo preparar $target." }
    }
}

$assets = Join-Path $project '.build/assets'
$licenses = Join-Path $assets 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$bundle = Join-Path $assets 'game-assets.zip'
if (Test-Path -LiteralPath $bundle) { Remove-Item -LiteralPath $bundle }
[System.IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $game 'assets'), $bundle)
Copy-Item -LiteralPath (Join-Path $sdl 'LICENSE.txt') -Destination (Join-Path $licenses 'SDL2.txt') -Force
Copy-Item -LiteralPath (Join-Path $gl 'LICENSE') -Destination (Join-Path $licenses 'GL4ES.txt') -Force
Copy-Item -LiteralPath (Join-Path $game 'CREDITOS_AUDIO.md') -Destination $licenses -Force
Get-ChildItem -LiteralPath (Join-Path $game 'third_party') -File |
    Where-Object { $_.Extension -in '.txt', '.md' } |
    Copy-Item -Destination $licenses -Force
"sdk.dir=$($SdkPath.Replace('\','/'))" | Set-Content -LiteralPath (Join-Path $project 'local.properties') -Encoding ascii
Push-Location -LiteralPath $project
try {
    $gradleArguments = @('--console=plain', "-Psdl2Source=$sdl", ':app:assembleDebug')
    if ($Abis.Count -eq 1) { $gradleArguments += "-PtargetAbi=$($Abis[0])" }
    & .\gradlew.bat @gradleArguments
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo empaquetar el APK.' }
    Write-Output (Join-Path $project 'app/build/outputs/apk/debug/app-debug.apk')
} finally {
    Pop-Location
}
