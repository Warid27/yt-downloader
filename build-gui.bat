@echo off
setlocal

set "ROOT=%~dp0"
set "SRC=%ROOT%yt_downloader_gui.cpp"
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
cl /nologo /std:c++17 /EHsc /MT /DUNICODE /D_UNICODE "%SRC%" /Fe:"%OUT%" /link user32.lib shell32.lib comctl32.lib ole32.lib
exit /b %ERRORLEVEL%

:build_mingw
echo Building with MinGW-w64...
g++ -std=c++17 -municode -mwindows -static -static-libgcc -static-libstdc++ "%SRC%" -o "%OUT%" -lcomctl32 -lole32
exit /b %ERRORLEVEL%
