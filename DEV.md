# Developer Notes

Internal architecture and maintenance guide for the two downloader scripts.

## 📐 Architecture

Both scripts share the same structure — `youtube-downloader-audio.bat` is a fork of `youtube-downloader.bat` with audio-specific differences in three places (config block, output dir, yt-dlp flags). The two `.bat` files are intentionally duplicated rather than calling a shared helper, because batch has no `include`/`source` and `call :label` across files is fragile (delayed-expansion scope leaks).

### Pipeline

```
┌─────────────────────────────────────────────────────┐
│ STEP 1: locate yt-dlp.exe                          │
│   • local: %SCRIPT_DIR%yt-dlp.exe                  │
│   • fallback: PATH                                 │
│   • last resort: download from GitHub              │
├─────────────────────────────────────────────────────┤
│ STEP 1b: auto-update yt-dlp                        │
│   • yt-dlp --update-to nightly                    │
│   • fallback: direct PowerShell download           │
├─────────────────────────────────────────────────────┤
│ STEP 2: locate ffmpeg.exe                          │
│   • local: %SCRIPT_DIR%ffmpeg-.../bin/ffmpeg.exe   │
│   • fallback: PATH                                 │
│   • last resort: download + extract from gyan.dev  │
├─────────────────────────────────────────────────────┤
│ STEP 3: get root dir, validate video.txt/audio.txt │
│   • create template if missing                     │
├─────────────────────────────────────────────────────┤
│ STEP 4: parse URLs into URL1..URLN variables       │
│   • skip blank lines and # comments                │
├─────────────────────────────────────────────────────┤
│ STEP 5: confirm with user, init logs               │
├─────────────────────────────────────────────────────┤
│ STEP 6: download loop with retry                   │
│   • nested for /L: outer = URLs, inner = attempts  │
│   • skip subsequent attempts on success            │
├─────────────────────────────────────────────────────┤
│ STEP 7: summary + cleanup                          │
└─────────────────────────────────────────────────────┘
```

## 📁 File Layout

```
E:\yt-downloader\
├── youtube-downloader.bat         ← video script (entry point)
├── youtube-downloader-audio.bat   ← audio script (entry point)
├── yt-dlp.exe                     ← binary, auto-updated
├── ffmpeg-master-latest-win64-gpl-shared\
│   └── bin\ffmpeg.exe             ← binary, auto-extracted
├── video.txt                      ← URL list for video
├── audio.txt                      ← URL list for audio
├── results\
│   ├── *.mp4                      ← video output
│   ├── audio\*.mp3                ← audio output
│   ├── download_log.txt           ← video run history
│   ├── download_log_audio.txt     ← audio run history
│   ├── error_log.txt              ← failed video URLs
│   └── error_log_audio.txt        ← failed audio URLs
└── readme.md                      ← user docs
└── DEV.md                         ← this file
```

## 🔧 Config Block

Top of each script, easily editable:

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

## 🧬 Why Batch?

The user asked for batch. Pros: zero install, works on any Windows, double-click run. Cons: limited string handling, no proper arrays, `set /p` requires CRLF on piped stdin.

### Workarounds used

- **Indexed variable "arrays"**: `URL1`, `URL2`, ... parsed from text file. No real arrays in batch.
- **`setlocal enabledelayedexpansion`**: required for `!VAR!` expansion inside `for` loops.
- **`%VAR%` vs `!VAR!`**: `%VAR%` = parse-time (immediate), `!VAR!` = runtime (delayed). Mixing them is the #1 source of bugs.
- **No `goto` into `for` blocks**: used nested `for /L` instead for retry loop.
- **Trailing backslash on SCRIPT_DIR**: `set "SCRIPT_DIR=%~dp0"` returns `E:\App\yt-downloader\` with trailing `\`. We strip it with `set "DEFAULT_ROOT=%SCRIPT_DIR:~0,-1%"`. Critical: forgetting this breaks `set "FOO=%SCRIPT_DIR%yt-dlp.exe"` → would become `E:\App\yt-downloaderyt-dlp.exe`.

## 🐛 Known Quirks

### `set /p` and line endings
Windows `set /p` reads until `\r\n`. Piped input from Unix tools (Git Bash, coreutils) uses bare `\n`, which causes `set /p` to consume the entire stream as one value. **Test workaround**: use `printf '\r\nY\r\n'` not `printf '\nY\n'`. Real keyboard input is always CRLF, so end users never see this.

### ANSI color codes
`for /F "tokens=*" %%i in ('echo prompt $E ^| cmd') do set "ESC=%%i"` — extracts ESC character (0x1B) by querying cmd's prompt. This is a hack. On Windows <10 build 1903, ANSI may not render. No fallback — users on older Windows will see literal `[92m` etc.

### yt-dlp SABR streaming warnings
YouTube (mid-2025+) serves a new format type that some clients can't parse. Warnings like `n challenge solving failed` and `SABR-only streaming experiment` are emitted but downloads still work. If audio quality drops, switch `--extractor-args`:
```bat
--extractor-args "youtube:player_client=tv"  ← fewer restrictions, needs PO token for some
```

### PO Token workaround
If downloads start returning HTTP 403, YouTube wants a "GVS PO Token". The cleanest workaround is `yt-dlp --extractor-args "youtube:po_token=web.gvs+XXX"` but tokens expire. Long-term: keep `--update-to nightly` and accept occasional failures.

## 🔄 Self-Update Mechanism

### yt-dlp (Step 1b)
```bat
"!YTDLP!" --update-to nightly 2>&1
if !ERRORLEVEL! neq 0 (
    call :updateYtdlp   ← PowerShell download from GitHub
)
```

`yt-dlp --update-to` is preferred because yt-dlp knows its own version. Falls back to raw download if the network blocks PyPI (yt-dlp's update server).

If `yt-dlp.exe` is locked (running), we can't overwrite. The `move /y X X.old` trick lets us swap atomically — Windows allows renaming a running exe.

### ffmpeg (Step 2)
```bat
set "FFMPEG_URL=https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
```
~100MB download, extracted to local dir. Alternatives:
- BtbN's GitHub releases: `https://github.com/BtbN/FFmpeg-Builds/releases` — slightly newer, slightly bigger
- `https://github.com/GyanD/codexffmpeg/releases` — gyan's GitHub mirror

We use gyan.dev because it's stable and the URL doesn't change.

## 🔁 Retry Loop

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

Why not `goto :retryLoop`? **`goto` into a `for` block is undefined behavior in cmd** — it can leave the loop in a bad state. Nested `for /L` is the idiomatic way.

## 🧪 Testing

### Interactive
Double-click the script. Fastest feedback loop.

### Piped (CI/automation)
```bat
:: CRLF-terminated input file
printf '\r\nY\r\n' > inp.txt
cmd //c ".\youtube-downloader-audio.bat" < inp.txt
```

The first `\r\n` is the root dir prompt (default = script dir). `Y\r\n` confirms start.

### Smoke test
1. Use Rick Astley (`dQw4w9WgXcQ`) — never gets taken down, has every format.
2. Use a Shorts URL — different code path in extractor.
3. Use a music video with clear music — exercises MP3 metadata embed.

## 🚧 Maintenance Tasks

| When | Task |
|------|------|
| YouTube changes extractor | Wait for nightly build. Script auto-updates. |
| yt-dlp `--update-to` stops working | Check `https://github.com/yt-dlp/yt-dlp` for repo change. Update `DL_URL` in `:updateYtdlp`. |
| ffmpeg download URL breaks | Update `FFMPEG_URL` in `:updateFfmpeg`. |
| New format selector syntax | yt-dlp docs: <https://github.com/yt-dlp/yt-dlp#format-selection> |
| yt-dlp adds new flag we want | Add to download loop. Test against a known URL. |

## 📜 Versioning

We don't version these scripts — they're a thin wrapper. `yt-dlp --version` output in the log shows the tool version at run time. The script's own behavior is whatever was last edited.

## 🤝 Contributing Changes

When editing:

1. **Test BOTH scripts** — they should stay in sync for engine code (Steps 1–3, 5, 7).
2. **Audio vs video differences are intentional**:
   - `INPUT_FILENAME` (video.txt / audio.txt)
   - `OUTPUT_DIR` (`results` vs `results\audio`)
   - Log filenames (`download_log.txt` vs `download_log_audio.txt`)
   - yt-dlp flags (`-f bv*[height<=1080]+ba/b --embed-subs` vs `-f bestaudio/best -x --audio-format mp3`)
3. **Don't remove debug echos** without testing — but do remove them before committing.
4. **Add config vars at the top**, not hardcoded mid-script.

## ⚖️ Tradeoffs

- **Two scripts vs one with a flag**: chose two scripts because the audio/video flows diverge enough (no subs, no thumbnail convert, no height cap) that a flag would add 30+ lines of branching.
- **No shared helper file**: would require `call` with careful variable scoping. The duplication is ~80 lines of self-update code, worth the simplicity.
- **No PowerShell Core**: some Windows installs lack it. We use `powershell -NoProfile -Command` which is Windows PowerShell 5.1+ (ships with Win10+).
- **`set /p` instead of `choice`**: `choice` exists but doesn't echo user input back, which feels broken in interactive scripts.
