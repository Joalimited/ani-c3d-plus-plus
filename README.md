# ani-c3d

**ani-3d, rewritten in C++.** A 3D anime and manga browser in the style of the "File System Navigator" from
*Jurassic Park*. Search for a show, fly over its posters on glowing pedestals, pick an episode from a field of blocks and
it plays in mpv. Your shows also live on a walkable bookshelf, manga open as real books you can flip through, and there
are themes, mini-games and trophies on top.

Every feature of the Python [ani-3d](https://github.com/Joalimited/ani-3d) is here - the same world, sources, library,
book reader, eight themes, seven arcade games and trophies - as one native program built on **SDL2 + OpenGL 3.3**. It
starts in well under a second, the Linux AppImage is about 12 MB and the Windows exe is a single file.

Runs on Linux and Windows with any GPU driver that supports OpenGL 3.3. On a Windows PC without a graphics driver (a
virtual machine, for example) it falls back to a bundled software renderer by itself. There is also an **Android / Fire
TV build** with a remote-control dashboard - see [Fire TV / Android](#fire-tv--android).

## Getting started

### Linux (AppImage)

1. Install **mpv** (it plays the episodes):

   ```sh
   sudo dnf install mpv        # Fedora
   sudo apt install mpv        # Debian / Ubuntu
   ```

2. Build the AppImage (see [Building](#building)) or use one you already have, then run it:

   ```sh
   chmod +x ani-c3d-x86_64.AppImage
   ./ani-c3d-x86_64.AppImage
   ```

### Windows (exe)

`ani-c3d.exe` is a single file with mpv, the font and a software renderer bundled inside - just double-click it. It
needs Windows 10 or newer.

### Checking your setup

`ani-c3d --check` runs a self-check (graphics, sources, the history database, mpv, fonts) and prints what works and what
doesn't. On Windows give it a file to write to: `ani-c3d.exe --check check.txt`.

If you used the Python ani-3d before, ani-c3d picks up the same watch history and, on its first start, your ani-3d
settings (theme, reading direction, skip switches, recent librarian searches).

## Fire TV / Android

A separate APK build runs the same app on an Amazon **Fire TV Stick** (tested on the Fire TV Stick 4K Max, Fire OS 7 /
Android 11) and on other Android TV / Android 7+ devices with OpenGL ES 3.0. It is made for the remote, not a mouse and
keyboard: the 3D world becomes a dashboard in the spirit of the original Xbox dashboard, with the same neon themes.

### Install on a Fire TV Stick

1. Get `ani-c3d-firetv.apk` from the [Releases](../../releases) page (or build it, see below).
2. On the stick enable **Settings > My Fire TV > Developer Options > ADB debugging** and
   **Install unknown apps** for the app you sideload with. (If Developer Options is missing: Settings > My Fire TV >
   About, then press Select on the device name seven times.)
3. Note the stick's IP address: **Settings > My Fire TV > About > Network**.
4. From a computer on the same network:

   ```sh
   adb connect 192.168.1.50:5555          # use your stick's IP; accept the "Allow USB debugging?" prompt on the TV
   adb install -r ani-c3d-firetv.apk      # -r keeps your watchlist and history when updating
   adb shell am start -n com.ani.c3d/.MainActivity
   ```

   No `adb` installed? On Fedora `sudo dnf install android-tools`, on Debian / Ubuntu `sudo apt install adb`, or use
   the build container (below). A sideloading app such as *Downloader* on the stick also works if you host the APK.
5. The app shows up under **Apps & Games** as **ani-c3d**. Useful extras:

   ```sh
   adb logcat -s ani-c3d                  # fps, key presses, subtitles, errors
   adb shell am force-stop com.ani.c3d    # close it
   adb uninstall com.ani.c3d              # remove it (and its data)
   ```

### Remote controls

| Where | Button | Does |
| --- | --- | --- |
| Dashboard | Left / Right | switch category (Currently Watching, Recommended, Popular, ... then WATCHING, DISCOVER, MANGA, SEARCH, HISTORY, WATCHLIST, THEME) |
| | Up / Down | move through the titles of a category |
| | Select | open a title (or run the focused tab; SEARCH opens the Fire TV keyboard) |
| | Menu | jump to the tab strip and back |
| | Back | close / go up; at the top it leaves the app |
| Title page | Select | START / CONTINUE, ADD TO WATCHLIST, or play the focused episode tile |
| Playing | Menu | show / hide the controls |
| | Select | take the SKIP INTRO / CREDITS prompt, otherwise pause and show the controls |
| | Left / Right | seek -10 s / +10 s |
| | Up / Down | volume (the app's own, with an on-screen bar) |
| | Back | stop and return |
| Manga reader | Left / Right | turn the page (the way the book reads; right-to-left by default) |
| | Up / Down | scroll a long strip, or turn the page |
| | Rewind / Fast-forward | previous / next chapter |
| | Menu | flip the reading direction |

Easter egg: **Up Up Down Down Left Right Left Right** sends an anime cat bouncing around the screen like the old DVD
logo (it also wanders in by itself after 30 seconds of doing nothing, and one time in thirty it is a Nyan Cat).

### Notes and limits

- **Volume +/- on the remote** belong to Fire OS: it hands them to your TV or soundbar over HDMI-CEC and apps never see
  them. Use Up / Down while a video plays for the app's own volume.
- **Playback** uses Android's built-in player (H.264 / HLS), not mpv. Subtitles (WebVTT / SRT) are drawn by the app.
- **Skip intro / credits** use timestamps from the stream, from chapter marks or from the crowd-sourced
  [AniSkip](https://aniskip.com/) database; the button only shows while an intro or credits scene is really on screen.
  The intro switch is one-shot (skips the next episode's intro once); the credits switch applies to every episode.
- **Performance:** the TV build renders at 720p internally with light anti-aliasing and the plain grid floor, which holds
  60 fps on a Fire TV Stick 4K Max. Tweak without rebuilding:
  `adb shell setprop debug.anic3d.scale 0.75` (render scale), `.msaa 0|2|4`, `.ui 1.3` (interface size), then relaunch.
- **Themes** work as on the desktop; the TV keeps the creatures and props in the background and drops the static
  scenery so every theme stays smooth. The CRT television only floats in the sparse themes.
- The first run opens on **Currently Watching**; on a fresh install with no history it shows what is popular.

### Building the APK

The Android toolchain (NDK, SDK build-tools, a JDK and the native libraries cross-built for arm64-v8a / armeabi-v7a)
lives in a second container image:

```sh
podman build -t ani-c3d-android -f tools/Containerfile.android tools
mkdir -p build/android/deps build/android/src
for abi in arm64-v8a armeabi-v7a; do         # once: curl, OpenSSL, PCRE2, libwebp, SQLite, SDL2 for each ABI
  podman run --rm -v "$PWD:/work:Z" -e SRC_CACHE=/work/build/android/src ani-c3d-android \
      /work/tools/android/build_deps.sh $abi /work/build/android/deps/$abi
done
podman run --rm -v "$PWD:/work:Z" ani-c3d-android /work/tools/android/build_apk.sh
# -> dist/ani-c3d-firetv.apk (debug-signed, both ABIs; the stick uses armeabi-v7a)
```

The APK is signed with a debug key kept at `build/android/debug.keystore` - back it up, or a rebuilt APK will not install
over an installed one without uninstalling first. Set `ABIS=arm64-v8a` to build one ABI only. The TV dashboard can also
be tried on a desktop with `ANIC3D_TV=1 ./ani-c3d` (and a GLES-only build with `-DANI_GLES=ON`).

## First steps

- The app opens on **HOME**: shows to continue watching, recommendations and what's popular. Click a poster to open it,
  then click an episode block to play it. Continue-watching shows resume where you stopped.
- Press **/** to search. Hover a poster to see its description.
- **L** opens the anime library (a bookshelf of what you watch), **M** the manga library, **G** the arcade, **T** cycles
  the theme, **H** shows your history, **U** hides the interface.

| Where | Keys |
| --- | --- |
| World | drag = orbit, right-drag = pan, wheel = zoom, `Esc` = back, `space` = pause, arrows = seek / volume, `n` / `p` = next / previous episode, `d` = sub / dub |
| Library | `A`/`D` walk, `W`/`S` climb, `Enter` open a book, `F` star (anime) or add to Going To Read (manga), `X` remove, drag books between shelves |
| Open book | arrows or `A`/`D` turn pages, `[` `]` previous / next chapter, `O` chapter list, wheel or `+`/`-` zoom, `Z` fit page, `G` reading direction, `V` scroll / book |

## Themes

Classic FSN, Jurassic Park (a herd of dinosaurs you can feed and follow), Enchanted Forest (frogs, ladybugs, beetles and
butterflies), Sakura Garden, Halloween (a haunted graveyard), Matrix (falling digits), Retro Arcade (a striped synthwave
sun), Old VCR (floating VHS tapes, cassettes, Walkmans and Macintoshes) and **Media Center 2005** (Windows XP Media
Center blues with butterflies). Press **T** to switch; the choice is remembered.

## Arcade

Snake, Breakout, Poster Quiz, Barrel Climb (on the bookshelf, five stages), Anime Trivia, Data Maze and Dino Galaga
(anime-character bosses). Press **G**, or **K** in the library for Barrel Climb.

## Where your data lives

| What | Linux | Windows |
| --- | --- | --- |
| Watch history, reading progress, shelves (shared with ani-3d / ani-cli) | `~/Watch-history.db` | `%USERPROFILE%\Watch-history.db` |
| Settings (theme, reader options) | `~/.config/ani-c3d/settings.json` | `%APPDATA%\ani-c3d\settings.json` |
| Cache (posters, lookups, manga pages) | `~/.cache/ani-c3d/` | `%LOCALAPPDATA%\ani-c3d\cache\` |
| Exported manga chapters (PDF) | `~/Documents/ani-c3d manga/` | `Documents\ani-c3d manga\` |

Set `ANI_CLI_WATCH_DB` to keep the history database somewhere else. Nothing is uploaded anywhere.

## Building

The builds run in a Podman (or Docker) container, so nothing needs to be installed on your machine. Build the image once:

```sh
podman build -t ani-c3d-build -f tools/Containerfile tools
```

- **Linux:** `tools/build.sh linux` builds `build/linux/ani-c3d`; then `tools/build_appimage.sh` packs
  `dist/ani-c3d-x86_64.AppImage`.
- **Windows:** `tools/build.sh windows` cross-compiles `build/windows/ani-c3d.exe` (MinGW, fully static). Set
  `ANI_MPV_ZIP` to a zip of a Windows mpv build (default `~/ani-3d-build/mpv.zip`) to bundle mpv into the exe.

To build natively instead, install a C++17 compiler, CMake and the development packages for SDL2, libcurl, PCRE2,
SQLite, zlib and libwebp, then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/ani-c3d
```

For testing there is `--snapshot out.png "query" [index] [WxH]` (renders a frame to a PNG without showing a window),
`--playtest "query"` (searches, plays episode 1 in mpv and reads the player state back), and `ANIC3D_SELFTEST=<seconds>`
(a scripted tour through search, games, both libraries and the book reader; add `ANIC3D_SHOTS=<dir>` for screenshots).

How the C++ code maps onto the original Python modules is in [`docs/PORTING.md`](docs/PORTING.md).

## Thanks

- **[ani-cli](https://github.com/pystardust/ani-cli)** - a huge thank you to the ani-cli project and its contributors.
  The scraper (`src/core/scrape.cpp`) follows ani-cli style scraping. Without ani-cli this project would not exist.
- [mpv](https://mpv.io/) for playback.
- [MangaDex](https://mangadex.org/), [AniList](https://anilist.co/) and [Jikan](https://jikan.moe/) (MyAnimeList) for
  their public APIs.
- [SDL](https://www.libsdl.org/), [libcurl](https://curl.se/), [PCRE2](https://www.pcre.org/), [SQLite](https://sqlite.org/),
  [libwebp](https://chromium.googlesource.com/webm/libwebp), [zlib](https://zlib.net/),
  [nlohmann/json](https://github.com/nlohmann/json), [stb](https://github.com/nothings/stb),
  [miniz](https://github.com/richgel999/miniz) and, on Windows, [OpenSSL](https://www.openssl.org/) and
  [Mesa](https://mesa3d.org/) ([mesa-dist-win](https://github.com/pal1000/mesa-dist-win)).
- [Noto Sans Mono](https://fonts.google.com/noto) (SIL Open Font License, see `assets/fonts/NOTO-LICENSE.txt`).

## Disclaimer

ani-c3d does not host any content. It finds streams and chapters on third-party websites, the same way ani-cli does. Use
it in line with the laws where you live and the terms of the sites involved.

## License

GPL-3.0, the same license as ani-cli and ani-3d. See [LICENSE](LICENSE).
