@echo off
setlocal EnableDelayedExpansion
set ROOT=%~dp0

call "%ROOT%build.bat" || exit /b 1

if not exist "%ROOT%bin\iliz_mcp_player.exe" (
  echo Missing bin\iliz_mcp_player.exe
  exit /b 1
)
if not exist "%ROOT%bin\bass.dll" (
  echo Missing bin\bass.dll — put it next to the exe in bin\
  exit /b 1
)

set VER=
for /f "tokens=3 delims= " %%a in ('findstr /C:"#define APP_VERSION_A" "%ROOT%src\appinfo.h"') do set VER=%%~a
set VER=!VER:"=!
if "!VER!"=="" (
  echo Could not read APP_VERSION_A from src\appinfo.h
  exit /b 1
)

set OUT=%ROOT%dist
if not exist "%OUT%" mkdir "%OUT%"
set NAME=iliz_mcp_player-!VER!
set STAGE=%OUT%\%NAME%
set ZIP=%OUT%\%NAME%.zip

if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%ZIP%" del /q "%ZIP%"
mkdir "%STAGE%"
copy /y "%ROOT%bin\iliz_mcp_player.exe" "%STAGE%\" >nul
copy /y "%ROOT%bin\bass.dll" "%STAGE%\" >nul

powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIP%' -Force" || exit /b 1
rmdir /s /q "%STAGE%"

echo Packed: %ZIP%
