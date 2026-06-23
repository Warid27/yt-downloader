@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>nul

REM ========================================
REM   YT-DLP Enhanced Audio Batch Downloader
REM   Self-updating + auto-retry + portable
REM   Extracts best audio -> MP3 (320kbps)
REM ========================================

REM --- CONFIG ---
set "SCRIPT_DIR=%~dp0"
set "DEFAULT_ROOT=%SCRIPT_DIR:~0,-1%"
set "INPUT_FILENAME=audio.txt"
set "AUDIO_FORMAT=mp3"
set "AUDIO_QUALITY=0"
set "RETRY_COUNT=3"
set "RETRY_DELAY=5"

REM --- BINARY LOCATIONS ---
set "YTDLP_LOCAL=%SCRIPT_DIR%yt-dlp.exe"
set "FFMPEG_DIR=%SCRIPT_DIR%ffmpeg-master-latest-win64-gpl-shared"
set "FFMPEG_BIN=%FFMPEG_DIR%\bin"
set "FFMPEG_EXE=%FFMPEG_BIN%\ffmpeg.exe"

REM --- COLOR CODES (Windows 10+ ANSI) ---
for /F "tokens=*" %%i in ('echo prompt $E ^| cmd') do set "ESC=%%i"
set "G=%ESC%[92m"
set "R=%ESC%[91m"
set "Y=%ESC%[93m"
set "B=%ESC%[94m"
set "C=%ESC%[96m"
set "N=%ESC%[0m"

cls
echo.
echo %B%========================================%N%
echo %B%   YT-DLP Audio Batch Downloader%N%
echo %B%   Output: MP3 (best quality)%N%
echo %B%========================================%N%
echo.

REM ============================================================
REM   STEP 1: ENSURE YT-DLP EXISTS + AUTO-UPDATE
REM ============================================================

set "YTDLP="
if exist "!YTDLP_LOCAL!" set "YTDLP=!YTDLP_LOCAL!"
if not defined YTDLP (
    where yt-dlp >nul 2>&1 && set "YTDLP=yt-dlp"
)

if not defined YTDLP (
    echo %R%yt-dlp not found. Downloading...%N%
    call :updateYtdlp
    if !ERRORLEVEL! neq 0 (
        echo %R%Failed. Install manually: https://github.com/yt-dlp/yt-dlp%N%
        pause & exit /b 1
    )
)

echo %C%yt-dlp: !YTDLP!%N%
echo %C%Version: %N%
"!YTDLP!" --version
echo.

REM --- Auto-update yt-dlp to nightly ---
echo %Y%Checking for yt-dlp updates...%N%
"!YTDLP!" --update-to nightly 2>&1
if !ERRORLEVEL! neq 0 (
    echo %Y%Built-in update failed, trying direct download...%N%
    call :updateYtdlp
)
echo.

REM ============================================================
REM   STEP 2: ENSURE FFMPEG EXISTS (REQUIRED for audio extraction)
REM ============================================================

set "FFMPEG_AVAIL=0"
if exist "!FFMPEG_EXE!" (
    set "PATH=!FFMPEG_BIN!;!PATH!"
    set "FFMPEG_AVAIL=1"
) else (
    where ffmpeg >nul 2>&1 && set "FFMPEG_AVAIL=1"
)

if "!FFMPEG_AVAIL!"=="0" (
    echo %R%ffmpeg not found. Required for audio extraction. Downloading...%N%
    call :updateFfmpeg
    if !ERRORLEVEL! neq 0 (
        echo %R%ffmpeg download failed. Audio extraction requires ffmpeg.%N%
        echo %R%Get manually: https://www.gyan.dev/ffmpeg/builds/%N%
        pause & exit /b 1
    )
)

echo %C%ffmpeg: %N%
ffmpeg -version
echo.

REM ============================================================
REM   STEP 3: GET ROOT DIRECTORY + INPUT FILE
REM ============================================================

echo %C%Download directory:%N%
echo %Y%(Press Enter for default: %DEFAULT_ROOT%)%N%
echo %Y%Input file: %INPUT_FILENAME%%N%
echo.
set "ROOT_DIR="
set /p "ROOT_DIR=Root directory: "
if "!ROOT_DIR!"=="" set "ROOT_DIR=%DEFAULT_ROOT%"
if "!ROOT_DIR:~-1!"=="\" set "ROOT_DIR=!ROOT_DIR:~0,-1!"

echo %G%Using: !ROOT_DIR!%N%
echo.

set "INPUT_FILE=!ROOT_DIR!\!INPUT_FILENAME!"
set "OUTPUT_DIR=!ROOT_DIR!\results\audio"
set "LOG_FILE=!ROOT_DIR!\results\download_log_audio.txt"
set "ERROR_LOG=!ROOT_DIR!\results\error_log_audio.txt"

if not exist "!ROOT_DIR!" mkdir "!ROOT_DIR!" 2>nul
if not exist "!OUTPUT_DIR!" mkdir "!OUTPUT_DIR!" 2>nul

if not exist "!INPUT_FILE!" (
    echo %Y%Creating template !INPUT_FILENAME!...%N%
    (
        echo # Add YouTube URLs below, one per line.
        echo # Lines starting with # are comments and ignored.
        echo # Example:
        echo # https://www.youtube.com/watch?v=dQw4w9WgXcQ
    ) > "!INPUT_FILE!"
    echo %G%Template created at: !INPUT_FILE!%N%
    echo Add URLs and run again.
    pause & exit /b 0
)

REM ============================================================
REM   STEP 4: PARSE URLS (single pass)
REM ============================================================

set "COUNT=0"

for /F "usebackq tokens=* delims=" %%A in ("!INPUT_FILE!") do (
    set "LINE=%%A"
    set "TRIM=!LINE: =!"
    if not "!TRIM!"=="" (
        set "FIRST=!LINE:~0,1!"
        if /i not "!FIRST!"=="#" (
            set /a COUNT+=1
            set "URL!COUNT!=!LINE!"
        )
    )
)

if !COUNT! equ 0 (
    echo %R%No valid URLs in !INPUT_FILE!%N%
    echo Add lines with http:// or https:// - one per line. # for comments.
    pause & exit /b 1
)

echo %G%Found !COUNT! URL^(s^):%N%
for /L %%i in (1,1,!COUNT!) do (
    set "DISPLAY_URL=!URL%%i!"
    echo   %C%%%i.%N% !DISPLAY_URL!
)
echo.

set /p "GO=Start audio extraction? [Y/N]: "
if /i not "!GO!"=="Y" (
    echo Cancelled.
    pause & exit /b 0
)

REM ============================================================
REM   STEP 5: INITIALIZE LOGS
REM ============================================================

(
    echo Audio extraction started: %DATE% %TIME%
    echo yt-dlp: !YTDLP!
    "!YTDLP!" --version
    echo Format: !AUDIO_FORMAT! (quality !AUDIO_QUALITY!)
    echo ========================================
) > "!LOG_FILE!"
echo. > "!ERROR_LOG!"

REM ============================================================
REM   STEP 6: EXTRACT AUDIO WITH RETRY
REM ============================================================

set "SUCCESS=0"
set "FAILED=0"

for /L %%i in (1,1,!COUNT!) do (
    set "URL=!URL%%i!"
    set "DONE=0"

    echo.
    echo %B%[%%i/!COUNT!] !URL!%N%
    echo [%%i/!COUNT!] !URL! >> "!LOG_FILE!"

    for /L %%a in (1,1,%RETRY_COUNT%) do (
        if "!DONE!"=="0" (
            echo %Y%  Attempt %%a/%RETRY_COUNT%...%N%

            "!YTDLP!" ^
                --extractor-args "youtube:player_client=web,android,ios" ^
                -f "bestaudio/best" ^
                -x ^
                --audio-format "!AUDIO_FORMAT!" ^
                --audio-quality "!AUDIO_QUALITY!" ^
                --no-playlist ^
                --embed-metadata ^
                --embed-thumbnail ^
                --convert-thumbnails jpg ^
                --retries 3 ^
                --fragment-retries 3 ^
                --no-overwrites ^
                --no-mtime ^
                -o "!OUTPUT_DIR!\%%(title)s.%%(ext)s" ^
                "!URL!"

            if !ERRORLEVEL! equ 0 (
                set "DONE=1"
            ) else (
                echo %Y%  Failed attempt %%a, waiting %RETRY_DELAY%s...%N%
                if %%a lss %RETRY_COUNT% timeout /t %RETRY_DELAY% /nobreak >nul
            )
        )
    )

    if "!DONE!"=="1" (
        echo %G%  OK%N%
        echo SUCCESS: !URL! >> "!LOG_FILE!"
        set /a SUCCESS+=1
    ) else (
        echo %R%  FAILED after %RETRY_COUNT% attempts%N%
        echo FAILED: !URL! >> "!ERROR_LOG!"
        echo FAILED: !URL! >> "!LOG_FILE!"
        set /a FAILED+=1
    )
)

REM ============================================================
REM   STEP 7: SUMMARY
REM ============================================================

echo.
echo %B%========================================%N%
echo %B%   Summary: %G%!SUCCESS! ok%N% / %R%!FAILED! fail%N%
echo %B%========================================%N%
echo Output: !OUTPUT_DIR!
echo Log:    !LOG_FILE!
echo Errors: !ERROR_LOG!

(
    echo.
    echo Done: %DATE% %TIME%
    echo Success: !SUCCESS! / Failed: !FAILED!
) >> "!LOG_FILE!"

if !FAILED! gtr 0 (
    echo.
    echo %Y%Some downloads failed. Common fixes:%N%
    echo 1. Run again - yt-dlp may have updated
    echo 2. Check !ERROR_LOG!
    echo 3. Add --cookies-from-browser chrome to yt-dlp args in script
)

echo.
pause
endlocal & exit /b 0


REM ============================================================
REM   HELPER: UPDATE YT-DLP (direct download)
REM ============================================================
:updateYtdlp
echo %Y%Downloading latest yt-dlp...%N%
set "TEMP_YTDLP=%TEMP%\yt-dlp-latest.exe"
set "DL_URL=https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"

powershell -NoProfile -Command ^
    "[Net.ServicePointManager]::SecurityProtocol='Tls12'; ^
     try { Invoke-WebRequest -Uri '!DL_URL!' -OutFile '!TEMP_YTDLP!' -UseBasicParsing; exit 0 } ^
     catch { Write-Host $_.Exception.Message; exit 1 }"

if !ERRORLEVEL! neq 0 (
    echo %R%Download failed. Check internet.%N%
    echo Manual: !DL_URL!
    exit /b 1
)

if exist "!YTDLP_LOCAL!" (
    move /y "!YTDLP_LOCAL!" "!YTDLP_LOCAL!.old" >nul 2>&1
)
move /y "!TEMP_YTDLP!" "!YTDLP_LOCAL!" >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo %R%Could not place yt-dlp.exe in script dir.%N%
    echo Saved at: !TEMP_YTDLP!
    exit /b 1
)

if exist "!YTDLP_LOCAL!.old" del "!YTDLP_LOCAL!.old" >nul 2>&1

set "YTDLP=!YTDLP_LOCAL!"
echo %G%yt-dlp downloaded: !YTDLP!%N%
"!YTDLP!" --version
exit /b 0


REM ============================================================
REM   HELPER: UPDATE FFMPEG (portable from gyan.dev)
REM ============================================================
:updateFfmpeg
echo %Y%Downloading ffmpeg portable (~100MB)...%N%
set "TEMP_DIR=%TEMP%\ffmpeg-dl-%RANDOM%"
set "ZIP_PATH=%TEMP_DIR%\ffmpeg.zip"
set "FFMPEG_URL=https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"

mkdir "!TEMP_DIR!" 2>nul

powershell -NoProfile -Command ^
    "[Net.ServicePointManager]::SecurityProtocol='Tls12'; ^
     try { Invoke-WebRequest -Uri '!FFMPEG_URL!' -OutFile '!ZIP_PATH!' -UseBasicParsing; exit 0 } ^
     catch { Write-Host $_.Exception.Message; exit 1 }"

if !ERRORLEVEL! neq 0 (
    echo %R%ffmpeg download failed.%N%
    echo Get manually: !FFMPEG_URL!
    rmdir /s /q "!TEMP_DIR!" 2>nul
    exit /b 1
)

echo %Y%Extracting ffmpeg...%N%
powershell -NoProfile -Command ^
    "Expand-Archive -Path '!ZIP_PATH!' -DestinationPath '!TEMP_DIR!' -Force"

set "FOUND_BIN="
for /D %%D in ("!TEMP_DIR!\ffmpeg-*") do (
    if exist "%%~fD\bin\ffmpeg.exe" set "FOUND_BIN=%%~fD\bin"
)

if not defined FOUND_BIN (
    echo %R%ffmpeg.exe not found after extraction.%N%
    rmdir /s /q "!TEMP_DIR!" 2>nul
    exit /b 1
)

if exist "!FFMPEG_DIR!" rmdir /s /q "!FFMPEG_DIR!"
move "!FOUND_BIN!\.." "!FFMPEG_DIR!" >nul 2>&1

if exist "!FFMPEG_EXE!" (
    set "PATH=!FFMPEG_BIN!;!PATH!"
    set "FFMPEG_AVAIL=1"
    echo %G%ffmpeg installed: !FFMPEG_EXE!%N%
    ffmpeg -version
) else (
    echo %R%ffmpeg install failed.%N%
)

rmdir /s /q "!TEMP_DIR!" 2>nul
exit /b 0
