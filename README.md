# ani-c3d

**ani-3d, rewritten in C++.** A 3D anime and manga browser in the style of the "File System Navigator" from
*Jurassic Park*. Search for a show, fly over its posters on glowing pedestals, pick an episode from a field of blocks and
it plays in mpv. Your shows also live on a walkable bookshelf, manga open as real books you can flip through, and there
are themes, mini-games and trophies on top.

Every feature of the Python [ani-3d](https://github.com/Joalimited/ani-3d) is here - the same world, sources, library,
book reader, eight themes, seven arcade games and trophies - as one native program built on **SDL2 + OpenGL 3.3**. It
starts in well under a second, the Linux AppImage is about 12 MB and the Windows exe is a single file.

Runs on Linux and Windows with any GPU driver that supports OpenGL 3.3. On a Windows PC without a graphics driver (a
virtual machine, for example) it falls back to a bundled software renderer by itself.

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
butterflies), Sakura Garden, Halloween (a haunted graveyard), Matrix, Retro Arcade and Old VCR. Press **T** to switch;
the choice is remembered.

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
