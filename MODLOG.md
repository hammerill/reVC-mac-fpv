# reVC-fpv — MODLOG

Development journal. Historical: when it disagrees with libfpv's
docs/INTEGRATION.md or libfpv.h, those win.

## Goal (user request, 2026-10-28)
libfpv integration for reVC-mac (GTA Vice City recompiled for macOS arm64,
`/Applications/GTA Vice City.app`), architecture like ~/Projects/gtasa-fpv,
standalone (nothing else to install), install style like the Hammerill
Trainer's macOS port (~/Projects/GrinchTrainer-III-VC-SA/macos).
- Kamikaze: supported in code, NOT mentioned in README/INSTALL, NOT in CI.
  Local builds use it (`-DREVC_FPV_KAMIKAZE=ON` + libfpv build/mac-kamikaze).
- CI/CD: write .github/workflows/release.yml but do NOT trigger on push
  (workflow_dispatch only for now). Never commit/push unless told.

## Environment
- M1, macOS 26.6. Game exe `Contents/MacOS/macosx-arm64-librw_gl3_glfw-oal`
  (ad-hoc signed, no hardened runtime, exports symbols). Launcher script
  `Contents/MacOS/reVC` cd's to Resources and execs the exe.
- reVC source reference: ~/Projects/re3 (branch miami). librw not vendored.
- Disassembly dump + symbol list + `word.py` (reads instruction words) in the
  session scratchpad (regenerate: `nm exe > syms.txt`, `otool -tV exe > dis.txt`).
- libfpv: ~/Projects/libfpv, builds build/mac and build/mac-kamikaze
  (rebuilt today: `cmake --build build/mac-kamikaze --target fpv`).

## Key findings
- Runtime code patching WORKS: mach_vm_protect(RW|VM_PROT_COPY) on the
  game's text page, write, back to RX, icache flush. MAP_JIT memory works
  without entitlements. (First test "crash" was me overwriting an adjacent fn.)
  Patch only while no other thread runs → all detours installed in the
  plugin constructor.
- Prologues may start with adrp (FindPlayerCentreOfWorld, RenderScene):
  hook.cpp relocates adr/adrp, refuses other PC-relative instrs.
- Offsets (verified in vc.cpp kChecks): CPad::DisablePlayerControls u16
  +0xF0 (Pads[0]); CPed health +0x44C, m_fRotationCur +0x46C, Dest +0x470;
  entity matrix +0x08 (pos +0x38); entity type low 3 bits of +0x60;
  CPlaceable::SetHeading takes the CMatrix (entity+8); Scene.camera = Scene[1];
  TheCamera.m_matrix = TheCamera+8; Im3DVertex 24 bytes (pos, rgba@0xc, u@0x10).
- RW state values (from Render2dStuff disasm): ZTEST 6, ZWRITE 8, SRCBLEND
  10, DESTBLEND 11, VERTEXALPHA 12, FOG 14, CULL 20; BLENDSRCALPHA 5,
  INVSRCALPHA 6, CULLNONE 1; TRILIST 3; Im3D flags 0x18 = XYZ|RGBA.
- Enums: frontend sounds 195 highlight / 196 select / 197 back / 198 fail;
  WEAPONTYPE_RAMMEDBYCAR 39; PEDPIECE_TORSO 0; EXPLOSION_GRENADE 0;
  SURFACE_PLASTIC 24; CReplay::Mode 1 = playback.
- Streaming: map models stream around TheCamera (so our camera does it);
  collision via CColStore::LoadCollision(FindPlayerCoors) → hooked: drone
  pos + AddCollisionNeededAtPosn(original) as 2nd point (VC's RC-toy trick);
  population via FindPlayerCentreOfWorld(_NoSniperShift) → drone pos.
- Design choice vs gtasa: Tommy stays at his spot (controls disabled via our
  pad bit 0x8000) as the visible pilot; no "world treats drone as player"
  body stand-in (simpler, satisfies the "pilot stays visible" requirement).
- FOV: CDraw::SetFOV takes 4:3 hfov; vertical = scale*tan(f/2), scale learned
  from RwCameraGetViewWindow each frame (default 0.75).
- The game stops rendering when its window is unfocused: `open "/Applications/GTA Vice City.app"`
  refocuses it. osascript keystrokes are NOT permitted on this machine.

## Code layout (src/)
plugin.cpp (GLFW interposing + detours + constructor, keys.txt test aid),
hook.cpp (arm64 detours), vc.cpp/h (symbols, offsets, checks), fpvapi
(dlopen libfpv from Frameworks), drone.cpp (session, camera, kamikaze
ending under #if LIBFPV_KAMIKAZE), input.cpp (GLFW keys→libfpv, mod keys
F7/C/R/Y, filtering what the game sees), render.cpp (GL3 lens/noise/static
post-process, OSD+overlay via libfpv docs/renderers/fpv_render_gl3.h,
shot.txt screenshots as PPM), model.cpp (procedural quad via engine Im3D
after RenderScene), world.cpp (CWorld::ProcessLineOfSight raycast),
effects.cpp, sfx.cpp, audio.cpp (CoreAudio AudioUnit), kamikaze.cpp
(only with REVC_FPV_KAMIKAZE), autotest.cpp.
Build: CMakeLists.txt links a GLFW stub (cmake/glfw_stub.c) with the game's
install name `@loader_path/../Frameworks/libglfw.3.dylib` → CI needs no game.
install.sh / uninstall.sh: copy reVC-fpv.dylib + libfpv.dylib (+license) to
Contents/Frameworks, generic launcher loading every present plugin
(reVC-fpv + HammerillTrainerVC), backup reVC.fpv-backup. deploy.sh = dev loop.
Data: Contents/Resources/fpv-data; log Contents/Resources/reVC-fpv.log.

## Fixes 2026-10-28 ~13:35
- FROZEN SCREEN (not the screen lock, I was wrong): librw uses VAOs but
  uploads Im2D/Im3D with glBufferData(GL_ARRAY_BUFFER) without rebinding,
  trusting its VBO stays bound (ARRAY_BUFFER is not VAO state). fpv_gl3
  left its VBO bound (and init left VAO 0) -> every engine 2D/3D batch drew
  stale vertices, postfx re-presented an old frame forever, even after the
  drone. render::frame now saves/restores ARRAY_BUFFER, VAO, (UN)PACK_ALIGNMENT.
- SKY SPLIT/TILT: TheCamera's CMatrix is at +0x00, not +0x08: CPlaceable has
  no vtable in reVC (entities are +8 because CEntity adds one). We were
  writing it shifted, so CClouds::RenderBackground's roll (Up x Forward at
  TheCamera+0x20/+0x10) and the other TheCamera-derived values were garbage;
  the image was right only because we also write the RW frame. Fixed
  off::CameraMatrix, new kCheck on RenderBackground+0x58.
- "No ground" in tests was the save: Tommy starts in the mansion INTERIOR
  (CGame::currArea != 0), so only all-area LODs drew. autotest.txt "at x y z"
  now teleports Tommy, sets currArea 0 + RemoveBuildingsNotInArea(0)
  (SET_AREA_VISIBLE 0), waits 3 s. Outdoor spot: at 262.0 -1270.0 11.0.
- Verified outdoors: FPV/chase/LOS, fisheye, OSD, model, game normal after.
- Test aid: wait ~35 s after launch before keys, else the key sequence can
  walk into Quit Game (game exited once that way, no crash).

## Status (2026-10-28 ~13:15)
- Save "Rub Out" loads (Load game > slot 1 > down enter = yes). Tommy in the
  mansion (interior, -378.5 -587.1 24.3).
- Autotest ran end to end from the log: launch with ground hit, arm, climb,
  ceiling impact (4 motor contacts + impact sfx), cameras FPV/chase/LOS
  (FOV 113.8/75/50, model 212 tris), end. Hammerill Trainer was loaded too
  (its swap hook chains into ours): coexistence works at load level.
- (superseded, see Fixes) the Mac's screen locked during the test
  (`lsappinfo front` = loginwindow, CGSSessionScreenIsLocked=1). While
  locked the game's frames don't reach the screen and our post pass
  re-distorts the stale back buffer (nested OSD copies). Not a plugin bug
  as far as known; re-check once unlocked. `screencapture -x` works on this
  machine (shows the real screen), handy to cross-check shot.txt.
- Written: README.md (no kamikaze), docs/INSTALL.md, LICENSE (GPL-3),
  .github/workflows/release.yml (workflow_dispatch only, macos-14). The CI
  steps were simulated locally (clean non-kamikaze build, zip layout OK,
  both dylibs arm64). Repo name assumed hammerill/reVC-mac-fpv in links.

## Next steps
1. With the screen unlocked: launch the drone OUTDOORS (walk out of the
   mansion, or Y after flying out) and check visually: FPV image, fisheye,
   OSD, chase/LOS model, overlay (F8), cursor, Esc while overlay open.
   If the nested-OSD image persists while unlocked, suspect GL state that
   librw caches (GL_ARRAY_BUFFER, UNPACK_ALIGNMENT left by fpv_gl3).
2. Move Tommy here (Y), respawn (R), kamikaze detonation + endings.
3. Verify ped heading write in move_here and ApplyMoveForce/InflictDamage.
4. Report to user; don't commit unless asked.
