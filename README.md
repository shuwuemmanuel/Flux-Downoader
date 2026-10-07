# ⚡ Flux Downloader (C++)

A full-featured download manager for Windows/macOS/Linux, written in **C++17 with Qt 6**,
styled with a classic **Apple / Aqua** look. It combines the core feature sets of
**IDM** (Internet Download Manager) and **FDM** (Free Download Manager), adds a
built-in **YouTube video / playlist / channel downloader**, and ships with a
**Chrome extension** for full browser integration.

This is a C++ port of the original Python/PyQt6 app. It keeps the same features,
the same Aqua light/dark design, the same settings file and the same Chrome
extension protocol. Existing settings in `~/.flux_downloader/settings.json`
carry over unchanged.

## Highlights

**Core download engine** (libcurl, one thread per connection)
- Segmented / multi-connection downloading with **adaptive segmentation** (2 connections for tiny files up to 32 for large ones, the way IDM scales connections to file size). Small files aren't over-split, and large files get full parallelism
- Each connection uses its own HTTP/1.1 socket with `Accept-Encoding: identity`, so byte-range math stays exact
- Pause & resume any download at any time. State is saved to disk, so **resume survives an app restart, even a crash**, and the download list is restored on the next launch
- Queue with a configurable number of simultaneous downloads (YouTube downloads go through the same queue)
- **FDM-style priority queue**: Highest/High/Normal/Low/Lowest. Higher-priority queued downloads jump the line and can **preempt** a running lower-priority download. The preempted download is stopped with its data kept on disk, and it resumes automatically when a slot frees up
- Global **and** per-download bandwidth throttling (token bucket)
- Automatic retry with backoff on network errors. Permanent errors (404, 403, …) fail immediately instead of retrying pointlessly
- Checksum verification after a download finishes: MD5 / SHA-1 / SHA-256 / SHA-512, auto-detected from the digest length (or `sha256:…`)
- Uses the server's `Content-Disposition` filename when the URL doesn't give a real one
- Never overwrites: a second `file.zip` becomes `file (1).zip`
- Auto-categorization into Video / Music / Programs / Compressed / Documents / Other, each with its own folder
- Batch import: paste a list of URLs and queue them all at once
- Full context menu with multi-select: pause/resume/cancel/priority/open folder/copy URL/remove + delete file

**IDM-style progress window**
- Live status, size, transfer rate, time left and resume capability
- **Real per-connection view**: a bar showing every connection's start position and progress, plus a table of what each connection is doing
- Per-download **Speed Limiter** tab and **Options on completion** tab (open file / open folder / close window)
- Optional "ask where to save" dialog before starting and a "download complete" dialog when finished

**BitTorrent support** (libtorrent-rasterbar, the engine used by qBittorrent and Deluge)
- Add torrents by magnet link or `.torrent` file
- DHT, Local Service Discovery, PEX, and UPnP/NAT-PMP port mapping
- **Per-file priority selection** (Skip / Low / Normal / High)
- Torrent-level queue priority (Highest → Lowest), separate global upload/download rate limits
- Pause / resume / remove (with or without deleting the data). Torrents are remembered across restarts
- The Chrome extension automatically catches `magnet:` links and `.torrent` downloads

**YouTube / playlist / channel downloader** (drives [yt-dlp](https://github.com/yt-dlp/yt-dlp))
- Paste a single video, a full playlist, or an entire channel URL
- Resolves and lists every video so you can select exactly which ones to grab
- Best quality, 1080p, 720p, or audio-only (MP3)
- Optional subtitle download
- **Livestreams**: downloads from the beginning of the stream and then asks whether to keep recording

**Browser integration (Chrome extension, Manifest V3)**
- Intercepts **every** browser download and sends it to Flux Downloader's multi-connection engine instead of Chrome's single-stream downloader
- Catches `magnet:` links and `.torrent` files and routes them to the torrent engine
- Right-click "Download with Flux Downloader" on any link, video, audio, or image
- One-click "Send to Flux Downloader" button on YouTube watch pages
- Talks to the desktop app over a local HTTP server (`127.0.0.1`, default port `38019`). Nothing leaves your machine

**Extras**
- Clipboard monitor: detects a downloadable link the moment you copy it and offers to grab it
- System tray with minimize-to-tray and completion notifications
- Single instance: launching it again brings the running window forward. `FluxDownloader <url|magnet|file.torrent>` hands the link to the running copy
- Aqua **light** and Aqua **dark** themes (switch in Settings, applied immediately)
- Proxy support
- Sidebar smart views: All / Active / Queue / Completed, plus per-category views

## Getting a Windows build

**Easiest: GitHub Actions.** Every push builds the app. Open the repository's
**Actions** tab, pick the latest *Build* run and download the
`FluxDownloader-windows-x64` artifact. It is a ready-to-run folder with
`FluxDownloader.exe`, the Qt DLLs, `yt-dlp.exe`, `ffmpeg.exe` and the Chrome
extension.

**Build it yourself** with `scripts\build_windows.bat`:

1. Install **Visual Studio 2022** (or the Build Tools) with *Desktop development with C++*.
2. Install **Qt 6** for *MSVC 2019/2022 64-bit* with the [Qt online installer](https://www.qt.io/download-qt-installer).
3. Install **vcpkg**:
   ```bat
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```
4. Open an **x64 Native Tools Command Prompt for VS 2022**, then:
   ```bat
   set QT_DIR=C:\Qt\6.7.3\msvc2019_64
   set VCPKG_ROOT=C:\vcpkg
   scripts\build_windows.bat
   ```
   vcpkg builds libcurl and libtorrent the first time (this takes a while).
   The result is in `dist\FluxDownloader\`, including yt-dlp and ffmpeg.

## Building on Linux / macOS

```bash
# Debian / Ubuntu
sudo apt install qt6-base-dev libcurl4-openssl-dev libtorrent-rasterbar-dev cmake
# macOS (Homebrew)
brew install qt curl libtorrent-rasterbar cmake

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/FluxDownloader
```

For YouTube downloads, put `yt-dlp` on your `PATH` (or next to the executable)
along with `ffmpeg`.

If libtorrent isn't available, the app still builds and runs. Torrent
features are disabled and everything else works. You can also turn torrents
off on purpose with `-DFLUX_WITH_TORRENT=OFF`.

### Tests

```bash
cmake -S . -B build -DFLUX_BUILD_TESTS=ON
cmake --build build -j
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

The tests run the real engine against an in-process HTTP server. They cover
segmented and single-connection downloads, pause/resume, cancel and delete,
checksums, permanent-error handling, priority preemption, speed limiting,
queue persistence and the browser-integration server. Set `FLUX_TEST_CLIP` to a
small `.mp4` and the yt-dlp integration is tested too.

## Installing the Chrome extension

1. Open `chrome://extensions` in Chrome.
2. Enable **Developer mode** (top right).
3. Click **Load unpacked** and select the `chrome_extension/` folder (it sits next to `FluxDownloader.exe`).
4. Make sure Flux Downloader is running. The extension popup shows a green dot when it's connected.

Or use **Settings → Browser Integration → ⚡ Auto-Install Extension**, which opens
a step-by-step guide in Chrome. On Windows, `setup_extension.bat` and
`diagnose_extension.bat` in the app folder walk you through setup and
troubleshooting.

## Project layout

```
CMakeLists.txt                build (Qt 6 + libcurl + optional libtorrent)
vcpkg.json                    Windows dependencies for vcpkg
src/
  main.cpp                    entry point, single-instance handling
  core/
    DownloadItem.*            download data model (incl. priority levels)
    DownloadEngine.*          segmented downloader (libcurl; pause/resume, throttling, retries, adaptive segmentation, checksums)
    SpeedLimiter.*            token-bucket bandwidth limiter
    QueueManager.*            concurrency, priority scheduling/preemption, retries, history, persistence
    TorrentEngine.*           libtorrent wrapper (magnet/.torrent, per-file priority, queue priority)
    YouTubeEngine.*           yt-dlp driver (probe + download + livestreams)
    ClipboardMonitor.*        clipboard link detection
    LocalServer.*             HTTP server for the Chrome extension
    SettingsManager.*         persisted JSON settings + history
  ui/
    MainWindow.*              main window: sidebar, table, toolbar, tray
    AddDownloadDialog.*       add single URL / batch dialog (with priority)
    AddTorrentDialog.*        magnet/.torrent dialog with per-file priority picker
    YouTubeDialog.*           YouTube/playlist/channel picker
    SettingsDialog.*          settings (incl. Torrents tab, extension installer)
    ProgressWindow.*          IDM-style per-download window
    DownloadDialogs.*         "start download" / "download complete" dialogs
resources/                    Aqua light/dark stylesheets, app icon
chrome_extension/             Manifest V3 extension (unchanged from the Python version)
scripts/                      Windows build script + extension helper scripts
tests/                        core engine tests (Qt Test)
```

The app keeps its config, history, download list and torrent list in
`~/.flux_downloader/` (`%USERPROFILE%\.flux_downloader` on Windows).

## What changed from the Python version

Everything works the way it did. Along the way, these problems from the
Python version were fixed:

- **Downloads now really resume after a restart.** The old app saved part files but never reloaded the download list, and it could lay out segments differently on resume and corrupt the file. The segment layout is now saved and reused.
- **No more silently broken files.** If a connection failed but its part file existed, the old app merged an incomplete file. Every segment's length is now checked before merging, and HTTP errors (e.g. a 404 page) are never saved as the file.
- **Paused downloads no longer hold a queue slot**, and a paused torrent stays paused (libtorrent's auto-queue used to resume it).
- The **progress window, start dialog and completion dialog** existed but were never shown. They're wired up now, and the progress window shows real per-connection data.
- **Torrent "Downloaded" column** always showed 0 B. It's fixed.
- **YouTube playlists** used to start every video at once (200 videos meant 200 yt-dlp processes). They now go through the queue and respect the concurrency limit. Cancel and pause work for YouTube items too.
- Videos are merged straight into MP4 (lossless remux) instead of re-encoded, which is much faster.
- A bare channel URL now lists the channel's videos instead of its tabs.
- Canceling the Add Torrent dialog after "Fetch File List" no longer leaves the torrent running.
- Fetching a `.torrent` from the browser no longer freezes the UI.
- MD5 checksums (suggested in the UI) used to always fail because SHA-256 was assumed. The algorithm is now auto-detected.
- Theme and browser-integration port changes apply immediately, without a restart.
- The server-provided filename (`Content-Disposition`) is used, the `Referer` from the browser is forwarded, and duplicate names get `(1)`, `(2)`, … instead of overwriting.
