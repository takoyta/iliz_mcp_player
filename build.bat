@echo off
setlocal
set ROOT=%~dp0
if not defined VSINSTALL set VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist "%ROOT%bin" mkdir "%ROOT%bin"
cd /d "%ROOT%bin"

rc /nologo /I"%ROOT%src" /foiliz_mcp_player.res "%ROOT%src\app.rc" || exit /b 1

cl /nologo /c /O2 /W0 /DSQLITE_ENABLE_FTS5 /DSQLITE_THREADSAFE=0 /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_DQS=0 ^
   "%ROOT%lib\sqlite3.c" || exit /b 1

cl /nologo /std:c++17 /O2 /W3 /DUNICODE /D_UNICODE /utf-8 ^
   /I"%ROOT%lib" /I"%ROOT%src" /Fe:iliz_mcp_player.exe ^
   "%ROOT%src\util.cpp" "%ROOT%src\tags.cpp" "%ROOT%src\store.cpp" ^
   "%ROOT%src\player.cpp" "%ROOT%src\http.cpp" "%ROOT%src\ui.cpp" "%ROOT%src\main.cpp" "%ROOT%src\media.cpp" ^
   sqlite3.obj iliz_mcp_player.res ^
   /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MANIFEST:NO ^
   "%ROOT%lib\bass64.lib" kernel32.lib user32.lib gdi32.lib advapi32.lib ^
   shell32.lib ws2_32.lib bcrypt.lib comctl32.lib runtimeobject.lib || exit /b 1

del /q *.obj *.res 2>nul
echo Built: %ROOT%bin\iliz_mcp_player.exe
