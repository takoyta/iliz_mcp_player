@echo off
setlocal
set ROOT=%~dp0
if not defined VSINSTALL set VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist "%ROOT%bin" mkdir "%ROOT%bin"
cd /d "%ROOT%bin"

rc /nologo /I"%ROOT%src" /foiliz_mcp_player.res "%ROOT%src\app.rc" || exit /b 1

cl /nologo /std:c++17 /O1 /Os /GS- /Gy /GR- /EHs-c- /W3 /DUNICODE /D_UNICODE ^
   /I"%ROOT%lib" /I"%ROOT%src" /Fe:iliz_mcp_player.exe "%ROOT%src\main.cpp" iliz_mcp_player.res ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:AppEntry /NODEFAULTLIB /OPT:REF /OPT:ICF ^
   /MANIFEST:NO "%ROOT%lib\bass64.lib" kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib ws2_32.lib || exit /b 1

del /q *.obj *.res 2>nul
echo Built: %ROOT%bin\iliz_mcp_player.exe
