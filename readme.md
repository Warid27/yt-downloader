# YT-DLP Enhanced Batch Downloader

A Windows YouTube downloader with batch scripts and a C++ Win32 GUI. The project wraps the real download engine provided by `yt-dlp` and the media processing provided by `ffmpeg`, so the repository stays small while the app can auto-download/update those runtime tools locally.

Repository: <https://github.com/Warid27/yt-downloader.git>

## Scripts

| Script | Input | Output | Format |
|--------|-------|--------|--------|
| `youtube-downloader.bat` | `video.txt` | `results\*.mp4` | Best video <=1080p + best audio, merged |
| `youtube-downloader-audio.bat` | `audio.txt` | `results\audio\*.mp3` | Best audio, extracted to MP3 320kbps |
| `yt-downloader-gui.exe` | `video.txt` / `audio.txt` | `results\*.mp4` / `results\audio\*.mp3` | C++ Win32 GUI for both modes |

Both scripts and the GUI share the same core dependencies: `yt-dlp` for fetching YouTube media and metadata, and `ffmpeg` for merging, converting, thumbnails, and audio extraction.

## Quick Start

### GUI (.exe)

1. Build the GUI with `build-gui.bat` (requires Visual Studio Build Tools `cl` or MinGW-w64 `g++`).
2. Run `yt-downloader-gui.exe`.
3. Choose the download root directory.
4. Edit `video.txt` / `audio.txt` in that root with YouTube URLs, one per line.
5. Use **Load Video URLs** / **Load Audio URLs** to preview, then **Download Video** / **Download Audio**.
6. Enable **Debug mode** to add `--verbose` to yt-dlp and show the exact command lines in the log.

### Batch scripts

1. Place the scripts in a folder:
   - `youtube-downloader.bat` (video)
   - `youtube-downloader-audio.bat` (audio)
   - `yt-dlp.exe` (downloaded automatically on first run if missing)
2. Double-click either script.
3. When prompted, enter the download root directory, or accept the default script directory.
4. Edit the generated `video.txt` / `audio.txt` with YouTube URLs, one per line.
5. Run the script again. It downloads everything, then prints a summary.

## Input File Format

```text
# Comments start with # (one per line)
https://www.youtube.com/watch?v=dQw4w9WgXcQ
https://www.youtube.com/watch?v=K4UNgJi6okM

# Mix channels, playlists, shorts - whatever yt-dlp accepts
https://youtu.be/jNQXAC9IVRw
```

## Auto-Update

Both scripts keep themselves current without manual work:

- `yt-dlp`: On every run, calls `yt-dlp --update-to nightly`. YouTube breaks extractors often, so this matters. If that fails, falls back to direct download from GitHub releases.
- `ffmpeg`: If not found in `PATH` or `./ffmpeg-master-latest-win64-gpl-shared/bin/`, downloads the portable build from gyan.dev and extracts it to the script directory.

## Folder Structure

```text
E:\yt-downloader\
  youtube-downloader.bat              Video script
  youtube-downloader-audio.bat        Audio script
  yt_downloader_gui.cpp               C++ Win32 GUI source
  build-gui.bat                       Builds yt-downloader-gui.exe
  yt-downloader-gui.exe               GUI app, after build
  yt-dlp.exe                          Auto-updated runtime binary
  ffmpeg-master-latest-win64-gpl-shared\
    bin\ffmpeg.exe                    Auto-extracted runtime binary
  video.txt                           Video URL list
  audio.txt                           Audio URL list
  results\
    *.mp4                             Downloaded videos
    audio\*.mp3                       Downloaded audio
    download_log.txt                  Video run history
    download_log_audio.txt            Audio run history
    error_log.txt                     Failed video URLs
    error_log_audio.txt               Failed audio URLs
```

## Default Config

Edit the `--- CONFIG ---` block at the top of each `.bat` to change defaults:

| Variable | Default | Purpose |
|----------|---------|---------|
| `DEFAULT_ROOT` | `%~dp0` (script dir) | Default download root |
| `RETRY_COUNT` | `3` | Attempts per URL on failure |
| `RETRY_DELAY` | `5` | Seconds between retries |
| `SUB_LANGS` | `en.*` (video only) | Subtitle language filter |
| `MAX_HEIGHT` | `1080` (video only) | Max video resolution |
| `AUDIO_FORMAT` | `mp3` (audio only) | `mp3`, `m4a`, `opus`, `flac`, `wav` |
| `AUDIO_QUALITY` | `0` (audio only) | `0` = best, `10` = worst (VBR scale) |

## Output Details

### Video (`youtube-downloader.bat`)

- Format: `bv*[height<=1080]+ba/b` - best video up to 1080p + best audio, merged
- Subtitles: English, embedded in MP4
- Thumbnail: embedded, converted to JPG
- Metadata: title, uploader, date embedded
- Container: MP4, or MKV if remux needed

### Audio (`youtube-downloader-audio.bat`)

- Format: `bestaudio/best` extracted to MP3 320kbps
- Thumbnail: embedded as cover art
- Metadata: title, artist, album embedded
- Container: MP3

## Logs

After every run:

- `results\download_log.txt` - every URL with SUCCESS/FAILED marker
- `results\error_log.txt` - only failed URLs, useful for retry

## Troubleshooting

### "yt-dlp is not installed or not in PATH"

Should not happen because the scripts auto-download it. If it does, check internet connectivity. Manual fallback: <https://github.com/yt-dlp/yt-dlp/releases/latest>

### "ERROR: Unable to extract audio" / "ffmpeg not found"

The scripts should download ffmpeg automatically. If that fails, manually grab the essentials build: <https://www.gyan.dev/ffmpeg/builds/>

### "n challenge solving failed" / SABR streaming warnings

YouTube changes its anti-bot measures. The script's auto-update to `nightly` usually fixes this within hours. If not, see `DEV.md` for PO Token notes.

### Downloads are slow or fail intermittently

- YouTube rate-limits aggressive downloading. The script already spaces retries by `RETRY_DELAY` seconds.
- If a specific URL fails repeatedly, try it alone. Sometimes single-video context succeeds where batch fails.

### Pipe testing from Git Bash

If you script the bat file with input redirection, use CRLF line endings. Bare LF confuses Windows `set /p`. Example:

```bash
printf '\r\nY\r\n' | cmd //c ".\youtube-downloader-audio.bat"
```

## Advanced

### Custom yt-dlp flags

Find the `for /L %%a in` download loop in either script and add flags after `-f`. Common additions:

- `--cookies-from-browser chrome` - for age-gated or private videos
- `--download-archive archive.txt` - skip already-downloaded URLs
- `--datebefore 20240101` - limit by upload date

### Per-URL format override

yt-dlp supports `# FORMAT=` comments in the URL file. The script does not parse these; use the `FORMAT` config var instead.

## License

Free to use and modify.

## Credits

- [yt-dlp](https://github.com/yt-dlp/yt-dlp) - YouTube downloader core
- [FFmpeg](https://ffmpeg.org/) - Audio/video processing core
- [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) - FFmpeg Windows builds

---

Note: Respect copyright laws and YouTube's Terms of Service. Only download content you have permission to download.
