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
- (superseded 21:20, stand-in ported) Design choice vs gtasa: Tommy stays at his spot (controls disabled via our
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
after RenderScene), world.cpp (CWorld::ProcessLineOfSight raycast), view.cpp (sky + sector scan
for any camera orientation), standin.cpp + pilot.cpp (as gtasa-fpv),
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

## Fixes 2026-10-28 ~21:20 (user's first test report)
- CORONAS / PICKUP GLOWS THROUGH WALLS: CCamera::Process copies the RW near
  plane (~0.9) into CDraw::ms_fNearClipZ before our hook sets ours
  (0.02-0.2). CSprite::CalcScreenZ maps depth with CDraw's near: sprites came
  out nearer than they are (a corona 10 m away depth-tested as ~2 m). Fix:
  apply_shot writes ms_fNearClipZ too.
- SKY GREY / WRONG WHEN ROLLED OR INVERTED: CClouds::RenderBackground and
  RenderHorizon draw screen-space bands above/below the horizon line with
  roll = acos (only +-90 deg), and UseDarkBackground paints flat grey (50) when
  forward.z < -0.9. In flight both are replaced (src/view.cpp): a 16x12 grid
  of DrawAnyRect coloured by each screen point's view-ray elevation (same
  colours/extents as the game: gradient over tan(elev) 0..0.5, fog to
  128*ambient below over 48/448). ms_cameraRoll set with atan2 for the clouds.
- GROUND / OBJECTS MISSING AT SOME ANGLES: CRenderer::ScanWorld picks sectors
  from a 2D triangle (camera + two far corners, top or bottom pair by at.z):
  a sliver when rolled, half the view when looking down. ScanSectorPoly is
  detoured (only inside ScanWorld): the hull of the camera + the 4 frustum
  corners at the same depth (LOD corners * 0.2 for the priority scan,
  LOD_DISTANCE = 300 * TheCamera+0xF8). The rasterizer wants CCW (checked
  against the game's own triangle orientation).
- STREAMING "BUGGED FAR AWAY": LoadCollision(drone) + second point Tommy
  keeps Tommy's boxes shrunk by 119 m, but CStreaming::Update then calls
  EnsureCollisionIsInMemory(FindPlayerCoors()) with 110 m: with Tommy 110-119 m
  from a box edge (and the drone outside it) the box was removed and
  synchronously reloaded (LOADCOL + CTimer::Suspend) every frame. Depends on
  where Tommy stands, hence "stopped after moving Tommy". Now FindPlayerCoors
  is the drone once the stand-in is on.
- STAND-IN + PILOT POSE (ported from gtasa-fpv standin.cpp / pilot.cpp, the
  earlier "no stand-in" design choice is dropped): beyond 6 m (back under
  3 m) Tommy goes with the drone hidden, collision-less, ProcessControl
  skipped (speeds zeroed), InflictDamage ignored. So pickups (player ped
  position within ~1.3 m), script door triggers (interiors), police, peds see
  him there. Thresholds low because VC's doors are script locates and he often
  launches next to one. Going home restores CGame::currArea (as
  SET_AREA_VISIBLE + RemoveBuildingsNotInArea) if the drone went through a
  door; Move Tommy here keeps the drone's area. Render swap: home + visible
  from ScanWorld's start (ConstructRenderList's prologue has a bl) to the end
  of RenderEffects. Pose: ANIM_STD_IDLE_CAM (7, partial+looped, ped.ifp),
  blended every step; released with blendDelta -4 + DELETEFADEDOUT. Radio:
  model.cpp, from the hand/finger/neck/head bones (VC ids 24/25/34/35/4/5).
  Wanted level doesn't decay while he stands in (CWanted::Update is in the
  frozen ProcessControl), as in gtasa-fpv.
- New offsets (all kChecks): RsGlobal.width +8, TheCamera LOD mult +0xF8,
  entity m_rwObject +0x58, flags bits from +0x60 (bUsesCollision 8, bIsVisible
  18, bOffscreen 42), CPhysical move/turn speed +0x84/+0x90, anim association
  blendDelta +0x2C, flags +0x3E.
- KAMIKAZE SWITCH SETUP (libfpv overlay, not this mod): after the Radio tab's
  wizard reaches Done it stays active until "Finish" (closing F8 keeps it), so
  the Kamikaze tab only said "A radio is being set up... Go there". Fixed in
  libfpv src/overlay/overlay.cpp: a finished, saved wizard of the other kind
  is dismissed when the tab opens (both directions). Uncommitted in libfpv.
- Verified: build, startup log (all 21 detours in, all offset checks pass).
  NOT verified in flight yet (game window needs focus): sky, scan, pose,
  radio placement, stand-in, interiors.

## Shared data folder (2026-10-29)
- libfpv's data_dir is now fpv_shared_data_dir() (~/Library/Application
  Support/libfpv, LIBFPV_DATA_DIR overrides), so the radio setup and tune
  follow the pilot between libfpv games. Fallback: the bundle's fpv-data.
- Migration in fpvapi.cpp: if the shared folder doesn't exist yet, the old
  Contents/Resources/fpv-data is copied into it (not shots/; old folder kept).
  If another game created it first, its setup wins and nothing is copied.
- The mod's own test triggers (autotest.txt, shot.txt, keys.txt) and shots/
  stay in Contents/Resources/fpv-data (paths::data_dir()).
- Verified with a harness (fake bundle, LIBFPV_DATA_DIR in scratch): copied
  once, shots skipped, second run left the shared folder alone. Not run in game.

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
