# Developer Notes

Internal architecture and maintenance guide for the downloader scripts and GUI.

## Architecture

`youtube-downloader-audio.bat` is a fork of `youtube-downloader.bat` with audio-specific differences in config, output directory, log files, and yt-dlp flags. The scripts intentionally duplicate engine code instead of using a shared helper because batch has no safe include/source mechanism and cross-file `call :label` is fragile.

The C++ GUI wraps the same runtime tools and keeps the same input files, output folders, retry behavior, and logs.

## Pipeline

```text
STEP 1: Locate yt-dlp.exe
  - Local: %SCRIPT_DIR%yt-dlp.exe
  - Fallback: PATH
  - Last resort: download from GitHub

STEP 2: Update yt-dlp
  - Run yt-dlp --update-to nightly
  - Continue with current copy if update fails

STEP 3: Locate ffmpeg.exe
  - Local: %SCRIPT_DIR%ffmpeg-master-latest-win64-gpl-shared/bin/ffmpeg.exe
  - Fallback: PATH
  - Last resort: download and extract from gyan.dev

STEP 4: Read root directory and input file
  - video.txt for video mode
  - audio.txt for audio mode
  - create a template input file if missing

STEP 5: Parse URLs
  - skip blank lines
  - skip lines starting with #

STEP 6: Download with retry
  - retry each URL up to RETRY_COUNT
  - log stdout/stderr
  - record failed URLs in error logs

STEP 7: Print summary
```

## File Layout

```text
E:\yt-downloader\
  youtube-downloader.bat              Video script
  youtube-downloader-audio.bat        Audio script
  yt_downloader_gui.cpp               C++ Win32 GUI source
  build-gui.bat                       Builds yt-downloader-gui.exe
  yt-dlp.exe                          Runtime binary, auto-updated
  ffmpeg-master-latest-win64-gpl-shared\
    bin\ffmpeg.exe                    Runtime binary, auto-extracted
  video.txt                           Video URL list
  audio.txt                           Audio URL list
  results\
    *.mp4                             Video output
    audio\*.mp3                       Audio output
    download_log.txt                  Video run history
    download_log_audio.txt            Audio run history
    error_log.txt                     Failed video URLs
    error_log_audio.txt               Failed audio URLs
```

## Core Dependencies

- `yt-dlp`: handles YouTube extraction, download format selection, metadata, thumbnails, and subtitles.
- `ffmpeg`: handles merge, audio extraction, conversion, metadata embedding, and thumbnail conversion.
- `gyan.dev`: source for portable Windows ffmpeg builds.

The repository should not commit `yt-dlp.exe`, ffmpeg binaries, downloaded media, logs, or built `.exe` files.

## Config Block

Top of each script:

```bat
set "SCRIPT_DIR=%~dp0"
set "DEFAULT_ROOT=%SCRIPT_DIR:~0,-1%"
set "RETRY_COUNT=3"
set "RETRY_DELAY=5"
```

Video adds:

```bat
set "FORMAT=bv*[height<=1080]+ba/b"
set "SUB_LANGS=en.*"
```

Audio adds:

```bat
set "AUDIO_FORMAT=mp3"
set "AUDIO_QUALITY=0"
```

## Batch Notes

- Indexed variable arrays use `URL1`, `URL2`, etc. Batch has no real arrays.
- `setlocal enabledelayedexpansion` is required for `!VAR!` expansion inside loops.
- `%VAR%` expands at parse time; `!VAR!` expands at runtime.
- Avoid `goto` into `for` blocks; nested `for /L` loops are safer for retries.
- `%~dp0` includes a trailing backslash, so the scripts strip it for `DEFAULT_ROOT`.

## Known Quirks

### set /p and line endings

Windows `set /p` reads until CRLF. Piped LF-only input from Git Bash or Unix tools can be consumed as one value. For automation, feed CRLF input.

```bash
printf '\r\nY\r\n' | cmd //c ".\youtube-downloader-audio.bat"
```

### ANSI color codes

The batch scripts query `cmd` for the ESC character and use ANSI color codes. Older Windows consoles may show raw color sequences.

### YouTube warnings

YouTube may emit SABR, challenge solving, or PO Token warnings. Downloads often still work. Keeping yt-dlp on the nightly build is the first fix.

## Self-Update Mechanism

### yt-dlp

```bat
"!YTDLP!" --update-to nightly 2>&1
if !ERRORLEVEL! neq 0 (
    call :updateYtdlp
)
```

`yt-dlp --update-to` is preferred because yt-dlp knows its own release channel. The direct GitHub download is a fallback.

### ffmpeg

```bat
set "FFMPEG_URL=https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
```

The scripts download the portable essentials build and extract it locally. Alternatives if gyan.dev changes:

- BtbN GitHub releases: `https://github.com/BtbN/FFmpeg-Builds/releases`
- GyanD GitHub mirror: `https://github.com/GyanD/codexffmpeg/releases`

## Retry Loop

```bat
for /L %%i in (1,1,!COUNT!) do (
    set "URL=!URL%%i!"
    set "DONE=0"
    for /L %%a in (1,1,%RETRY_COUNT%) do (
        if "!DONE!"=="0" (
            "!YTDLP!" ... "!URL!"
            if !ERRORLEVEL! equ 0 ( set "DONE=1" )
        )
    )
)
```

Do not replace this with `goto` inside a `for` block; that can leave `cmd` loop state inconsistent.

## Testing

### Interactive

Double-click the script or run the GUI.

### Piped

```bat
printf '\r\nY\r\n' > inp.txt
cmd //c ".\youtube-downloader-audio.bat" < inp.txt
```

### Smoke URLs

1. Use `dQw4w9WgXcQ` for a stable video with many formats.
2. Use a Shorts URL to cover a different extractor path.
3. Use a music video to exercise MP3 metadata embedding.

## Maintenance Tasks

| When | Task |
|------|------|
| YouTube changes extractor behavior | Wait for nightly build; the scripts auto-update. |
| yt-dlp update URL breaks | Check `https://github.com/yt-dlp/yt-dlp` and update the fallback URL. |
| ffmpeg download URL breaks | Update `FFMPEG_URL`. |
| Format selector syntax changes | Check yt-dlp format selection docs. |
| New yt-dlp flag is needed | Add it to the download command and test both scripts plus GUI. |

## Contributing Changes

1. Test video, audio, and GUI paths when changing shared download behavior.
2. Keep audio/video differences intentional and explicit.
3. Keep runtime binaries and generated files ignored.
4. Add config variables at the top instead of hardcoding mid-script.

## Tradeoffs

- Two scripts are easier for users than a mode flag.
- No shared batch helper keeps variable scoping simple.
- Windows PowerShell 5.1 is used instead of PowerShell Core because it ships with supported Windows versions.
- `set /p` is used instead of `choice` because it echoes user input in a clearer way.
