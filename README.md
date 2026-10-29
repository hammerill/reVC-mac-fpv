# GTA Vice City FPV (macOS)

Fly an FPV drone through Vice City with your own radio, on a Mac. Press F7
and Tommy launches a quad from the ground in front of him. It is simulated
by [libfpv](https://github.com/hammerill/libfpv), which runs real Betaflight
firmware, so rates, angle mode, air mode, arming and turtle mode behave as
they do on a real quad.

This is a plugin for **reVC**, the macOS (Apple Silicon) recompilation of
GTA Vice City (`GTA Vice City.app`). It is self-contained: nothing else to
install.

## Features

- **Real Betaflight** in the loop, with your rates, modes and tuning. Set up
  your radio once in the F8 overlay's wizard. Betaflight Configurator can
  connect too.
- **Three cameras**: FPV (the goggles view, with a fisheye lens and full
  roll), chase and line of sight from where Tommy stands. The chase and line
  of sight views show a small 5" quad.
- **Betaflight OSD** in the FPV view: battery, cell voltage, current, flight
  timer and mode. The GPS elements work too; home is Tommy.
- **Collides with the world**: buildings, the ground, cars, peds and props,
  through the game's own line-of-sight test.
- **Crash effects**: a fast hit hurts a ped (as if Tommy rammed them), dents
  a car or shoves a prop.
- **The city follows the drone**: buildings, collision, traffic and
  pedestrians stream in wherever you fly, while Tommy stands at his spot as
  the pilot, radio in hand.
- **The world treats the drone as Tommy**: a few metres out, Tommy goes along
  with it, unseen and unhurt, so the drone picks up pickups, flies through
  doors into interiors, and draws the police. He still shows at his spot and
  goes back there when the drone returns or the flight ends.
- **Motor sound** from libfpv's synthesizer.
- **Clean view**: the HUD, radar and radio station name are hidden while you
  fly.
- **Sound reactions** in the game's own sounds: a menu click on launch, the
  back sound on recall, a tick on a camera switch, the error buzz when
  something can't be done, and a collision sound on impacts from 3 m/s.
- **Move Tommy here**: ends the flight with Tommy (and his vehicle) under
  the drone, facing the way it faced.

## Controls

| Key | Action |
|---|---|
| F7 | Launch the drone / return to Tommy |
| F8 | Settings overlay (radio setup, rates, OSD, camera, fisheye strength) |
| C | Cycle cameras: FPV, chase, line of sight |
| R | Respawn the drone at its launch point, repaired |
| Y | Move Tommy here (also a button in F8 > Fly) |

Without a radio, the keyboard works as a fallback. Enter arms, W/S is
throttle, A/D is yaw, and the arrow keys are pitch and roll. A gamepad works
too.

## Requirements

- A Mac with Apple Silicon (M1 or later).
- GTA Vice City for macOS (reVC), normally at
  `/Applications/GTA Vice City.app`. Its `Contents/MacOS` holds a `reVC`
  launcher script and the `macosx-arm64-librw_gl3_glfw-oal` executable.
  The plugin checks the game's memory layout at startup; with another reVC
  build it logs the mismatch and stays off.

Works next to [Hammerill Trainer](https://github.com/hammerill/HammerillTrainer-III-VC-SA-macOS):
both installers write a launcher that loads every installed plugin.

## Installing

Players: see [docs/INSTALL.md](docs/INSTALL.md), which is also the text of
every release. Releases are built by `.github/workflows/release.yml` (run by
hand from the Actions tab) with the latest libfpv.
`.github/workflows/ci.yml` builds every push and PR against a pinned libfpv
(`LIBFPV_REF`) and, weekly, against libfpv main; it checks the plugin's
imports and that libfpv exports every function the plugin loads
(`tools/check_libfpv.cpp`). The game itself can't run in CI.

| File | What it is |
|---|---|
| `install.sh` | Copies the plugin into the app and edits its launcher |
| `uninstall.sh` | Undoes `install.sh` |
| `reVC-fpv.dylib` | The plugin |
| `libfpv.dylib` | libfpv, arm64 build |
| `libfpv.LICENSE.txt` | libfpv's license (GPL-3.0-or-later) |

The installer puts the dylibs in `GTA Vice City.app/Contents/Frameworks/`
and rewrites `Contents/MacOS/reVC` to load them through
`DYLD_INSERT_LIBRARIES` (the original is kept as `reVC.fpv-backup`).
Settings, radio mappings and Betaflight EEPROMs go in libfpv's shared
folder, `~/Library/Application Support/libfpv/`, so they follow you to
other libfpv games (`LIBFPV_DATA_DIR` moves it). The first run copies
what older versions kept in `Contents/Resources/fpv-data/`. The log is
`Contents/Resources/reVC-fpv.log`.

## Building

CMake 3.24 or newer and the Xcode command line tools.

```sh
# libfpv (from the libfpv checkout)
cmake -S . -B build/mac -DLIBFPV_BUILD_TESTS=OFF -DLIBFPV_BUILD_EXAMPLES=OFF
cmake --build build/mac -j

# this mod
cmake -S . -B build -DLIBFPV_DIR=~/Projects/libfpv
./deploy.sh   # builds and installs with ~/Projects/libfpv/build/mac/libfpv.dylib
```

Restart the game after `./deploy.sh`: the dylibs are loaded once, at launch.
The build doesn't need the game: it links against a GLFW stub with the
game's install name (`cmake/glfw_stub.c`).

## How it works

The game is ad-hoc signed without the hardened runtime, so the launcher's
`DYLD_INSERT_LIBRARIES` can load the plugin, and reVC exports its symbols,
so game functions are found by name. Every struct offset is in `src/vc.cpp`
and re-checked against the game's own instructions at startup.

- **Detours.** A few function entries (`CGame::Process`, `CCamera::Process`,
  `RenderScene`, the HUD, the streaming queries) are redirected by arm64
  detours (`src/hook.cpp`), installed from the plugin's constructor before
  the game starts any thread. Prologues with `adr`/`adrp` are relocated;
  any other PC-relative instruction makes the hook refuse and log.
- **GLFW.** dyld interposing on the GLFW functions the game imports: keys
  and mouse go to libfpv's overlay and keyboard fallback first, and
  `glfwSwapBuffers` draws the frame's extras.
- **Camera.** At the end of `CCamera::Process` the plugin steps libfpv and
  writes `TheCamera`'s matrix and the RenderWare camera, so the whole frame,
  culling included, uses the drone's camera with full roll. The FOV is
  calibrated from the RenderWare view window and widened for the fisheye to
  the real aspect; the near plane is pulled in front of the closest
  geometry.
- **World.** `CWorld::ProcessLineOfSight` for collision; streaming and
  population follow the drone through `FindPlayerCentreOfWorld` and
  `CColStore::LoadCollision` (with Tommy's spot kept loaded as well).
- **Drawing.** The fisheye, noise and static are an OpenGL 3 pass over a
  copy of the frame; the OSD and overlay use libfpv's
  `docs/renderers/fpv_render_gl3.h`. The drone model is drawn with the
  engine's Im3D right after `RenderScene`, tinted with the time of day's
  light.
- **Audio.** A CoreAudio output unit plays `fpv_audio_read`.

`MODLOG.md` is the development journal.

## Testing without hands

In `Contents/Resources/fpv-data/`: `autotest.txt` starts a scripted flight
(launch, arm, climb, forward, a screenshot per camera, land) once a game is
loaded; `shot.txt` saves the next frame to `shots/<time>.ppm`; `keys.txt`
presses keys (`down enter wait f7`...). Each file is deleted once used.

## Credits

- [libfpv](https://github.com/hammerill/libfpv): the drone, Betaflight
  (through SimITL), OSD, overlay and sound.
- [re3/reVC](https://github.com/halpz/re3) as the reference for the game's
  code and layouts.
- The quad model is procedural, made for this mod (`src/model.cpp`).
- Written with Claude (Anthropic).

## License

GPL-3.0-or-later, the same as libfpv (which embeds Betaflight). See
[LICENSE](LICENSE).
