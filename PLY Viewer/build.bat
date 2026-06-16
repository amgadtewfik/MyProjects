@echo off
REM Build PLY_Viewer.exe with icon and OpenGL 3.3 shaders
REM Requires MinGW-w64 with windres and gcc (e.g. x86_64-w64-mingw32-*).

set RC=windres
set CC=gcc
where x86_64-w64-mingw32-windres >nul 2>&1
if %ERRORLEVEL%==0 (
  set RC=x86_64-w64-mingw32-windres
  set CC=x86_64-w64-mingw32-gcc
)

echo Building resources...
%RC% ply_viewer.rc -O coff -o ply_viewer_res.o
if %ERRORLEVEL% neq 0 exit /b 1

echo Building PLY_Viewer.exe...
%CC% -o PLY_Viewer.exe ply_viewer.c ply_viewer_res.o -lopengl32 -lgdi32 -mwindows
if %ERRORLEVEL% neq 0 exit /b 1

echo Done: PLY_Viewer.exe
exit /b 0
