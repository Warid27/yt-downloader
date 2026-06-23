@echo off
setlocal

set "ROOT=%~dp0"
set "SRC=%ROOT%yt_downloader_gui.cpp"
set "OUT=%ROOT%yt-downloader-gui.exe"

where cl >nul 2>nul
if %ERRORLEVEL%==0 goto build_msvc

where g++ >nul 2>nul
if %ERRORLEVEL%==0 goto build_mingw

echo No supported C++ compiler found.
echo Install Visual Studio Build Tools ^(cl^) or MinGW-w64 ^(g++^), then run this script again.
exit /b 1

:build_msvc
echo Building with MSVC...
cl /nologo /std:c++17 /EHsc /DUNICODE /D_UNICODE "%SRC%" /Fe:"%OUT%" /link user32.lib shell32.lib comctl32.lib ole32.lib
exit /b %ERRORLEVEL%

:build_mingw
echo Building with MinGW-w64...
g++ -std=c++17 -municode -mwindows "%SRC%" -o "%OUT%" -lcomctl32 -lole32
exit /b %ERRORLEVEL%
