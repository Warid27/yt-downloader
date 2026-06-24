@echo off
setlocal

set "ROOT=%~dp0"
set "SRC=%ROOT%yt_downloader_gui.cpp"
set "RES=%ROOT%yt_downloader_gui.rc"
set "RES_OBJ=%ROOT%yt_downloader_gui.res"
set "OUT=%ROOT%yt-downloader-gui.exe"
set "VSDEVCMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"

where cl >nul 2>nul
if not errorlevel 1 goto build_msvc

if exist "%VSDEVCMD%" (
    call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
    where cl >nul 2>nul
    if not errorlevel 1 goto build_msvc
)

where g++ >nul 2>nul
if not errorlevel 1 goto build_mingw

echo No supported C++ compiler found.
echo Install Visual Studio Build Tools ^(cl^) or MinGW-w64 ^(g++^), then run this script again.
exit /b 1

:build_msvc
echo Building with MSVC...
rc /nologo /fo "%RES_OBJ%" "%RES%"
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /std:c++17 /EHsc /MT /DUNICODE /D_UNICODE "%SRC%" "%RES_OBJ%" /Fe:"%OUT%" /link user32.lib shell32.lib comctl32.lib ole32.lib
exit /b %ERRORLEVEL%

:build_mingw
echo Building with MinGW-w64...
windres "%RES%" -O coff -o "%RES_OBJ%"
if errorlevel 1 exit /b %ERRORLEVEL%
g++ -std=c++17 -municode -mwindows -static -static-libgcc -static-libstdc++ "%SRC%" "%RES_OBJ%" -o "%OUT%" -lcomctl32 -lole32
exit /b %ERRORLEVEL%
