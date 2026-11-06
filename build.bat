@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if not exist "%VSWHERE%" goto novs
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSPATH=%%i"
  if not defined VSPATH goto novs
  call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
)
where cmake >nul 2>nul
if errorlevel 1 goto novs

echo Richte das Projekt ein. Beim ersten Mal laedt das JUCE herunter...
cmake -B build -A x64
if errorlevel 1 goto failed

echo Baue Tekkdeck. Das dauert beim ersten Mal einige Minuten...
cmake --build build --config Release
if errorlevel 1 goto failed

set "OUT=build\Tekkdeck_artefacts\Release\VST3\Tekkdeck.vst3"
echo.
echo Fertig: %CD%\%OUT%
echo Kopiere den Ordner Tekkdeck.vst3 nach C:\Program Files\Common Files\VST3
echo und starte in FL Studio den Plugin-Scan.
explorer "%CD%\build\Tekkdeck_artefacts\Release\VST3"
pause
exit /b 0

:novs
echo Visual Studio mit "Desktopentwicklung mit C++" wurde nicht gefunden.
echo Installiere die kostenlose Community-Version und starte build.bat erneut.
pause
exit /b 1

:failed
echo Der Build ist fehlgeschlagen. Schick Claude die Fehlermeldung oben.
pause
exit /b 1
