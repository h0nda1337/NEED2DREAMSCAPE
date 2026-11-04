@echo off
setlocal
pushd "%~dp0game" || exit /b 1
set "launch=petscop_c89.exe"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0game\build.ps1" -OutputName NEED2DREAMSCAPE_compilado.exe -IfAvailable
if errorlevel 2 (
    echo Abriendo la compilacion incluida.
) else if errorlevel 1 (
    echo Fallo la compilacion local. Abriendo la compilacion incluida.
) else (
    set "launch=NEED2DREAMSCAPE_compilado.exe"
)
"%launch%" %*
set "result=%errorlevel%"
popd
if not "%result%"=="0" echo El juego termino con codigo %result%.
exit /b %result%
