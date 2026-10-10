# How ani-c3d maps onto ani-3d

ani-c3d is a module-by-module C++ port of the Python ani-3d. The structure, names and behaviour follow the original, so
a change in one is easy to find in the other.

| Python (`ani3d/`) | C++ (`src/`) | Notes |
| --- | --- | --- |
| `run.py` | `main.cpp` | window + event loop (SDL2), `--check`, `--snapshot`, self-test, crash dialog; adds `--playtest` |
| `app.py` | `app/app.cpp`, `app/app_hud.cpp` | engine glue, actions, input, frame / the HUD, overlays, hover hologram, player bar |
| `engine.py`, `mpvlink.py` | `core/engine.cpp`, `core/mpvlink.cpp` | mpv over JSON IPC: Unix socket / overlapped named pipe |
| `core.py` | `core/core.cpp` | watch db, cached backend, MangaDex, related content, HOME, blurbs |
| `scrape.py`, `multisrc.py`, `mangasrc.py` | `core/scrape.cpp`, `core/multisrc.cpp`, `core/mangasrc.cpp` | same regexes (PCRE2 behaves like Python `re`) |
| `advance.py`, `sections.py` | `core/advance.cpp`, `core/sections.cpp` | |
| `themes.py` | `world/themes.cpp` | settings live in `ani-c3d`; ani-3d's settings are imported on first start |
| `scene.py` | `world/scene.cpp` | |
| `effects.py`, `dinos.py`, `haunt.py`, `forest.py`, `sakura.py` | `world/*.cpp` | share `world/creatures.hpp` |
| `games.py`, `maze.py`, `galaga.py` | `games/*.cpp` | games reach the app through `GameHost` |
| `library.py`, `book.py` | `lib/library.cpp`, `lib/book.cpp` | books are shared `Book` objects (`lib/bookdata.hpp`) |
| `renderer.py`, `shaders.py` | `gfx/renderer.cpp`, `gfx/shaders.hpp` | wgpu/WGSL -> OpenGL 3.3/GLSL, same instancing and 4x MSAA |
| `text.py` | `gfx/text.cpp` | stb_truetype, same outlined text and digit atlas |
| `textinput.py`, `paths.py`, `mathx.py` | `app/textinput.hpp`, `util/paths.cpp`, `gfx/mathx.hpp` | |
| Pillow (`ImageDraw`, decode, PDF) | `gfx/canvas.cpp`, `gfx/image.cpp`, `util/pdf.cpp` | page art, thumbnails, chapter PDFs |
| `random.Random(seed)` | `util/pyrand.hpp` | Python's exact Mersenne Twister seeding, so every seeded layout (forest, graveyard, herd, garden) is identical |
| asyncio + threads | `app/tasks.cpp` | a worker pool; results come back to the render thread each frame |

## Differences on purpose

- **Graphics API:** OpenGL 3.3 instead of wgpu (Vulkan / Direct3D 12). It runs on every desktop GPU driver and builds
  into a small static Windows exe. On Windows without an OpenGL 3.3 driver the exe extracts a bundled Mesa (llvmpipe)
  and uses that, the way the Python build fell back to Direct3D 12's WARP renderer.
- **TLS on Windows:** curl on static OpenSSL with the Windows certificate store. MangaDex only accepts TLS 1.3, which
  Windows 10's own TLS stack (Schannel) does not offer.
- **Blender models:** ani-3d's dormant Blender mesh path (switched off since its revert) is not ported; every theme uses
  the box-built scenery and creatures, exactly what ani-3d shows.
- **Self-test timing:** `ANIC3D_SELFTEST` is time-based (ani-3d counted frames at ~60 fps), so it behaves the same on a
  165 Hz monitor.
- **Data folders:** settings, cache and exported PDFs use `ani-c3d` folders; the watch history database is shared with
  ani-3d and ani-cli.

## Environment variables

`ANI_CLI_WATCH_DB` (history database), `ANIC3D_THEME` (start theme), `ANIC3D_MPV_ARGS` (extra mpv options),
`ANIC3D_LOG` (log file), `ANIC3D_SELFTEST` / `ANIC3D_SHOTS` / `ANIC3D_SELFTEST_GAMES` (testing), `ANIC3D_NO_OPEN`
(don't launch browsers / viewers), `ANIC3D_SOFTWARE_GL=1` (force the software renderer). The `ANI3D_*` names from
ani-3d are accepted too.
