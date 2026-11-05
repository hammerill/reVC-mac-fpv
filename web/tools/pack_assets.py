#!/usr/bin/env python3
"""Copies the GTA Vice City files the web build needs into <out>/assets.

The page downloads every file listed in <out>/assets/manifest.json into the
browser's memory before the game starts, so only what the Demolition Man
demo uses goes in:

  - data/, models/ (gta3.img with the whole city), text/, txd/, anim/ped.ifp
  - anim/cuts.img and cuts.dir, cut down to the mission's cutscene (TEX_3)
  - audio/sfx.raw and sfx.sdt (every sound effect and ped voice), the
    cutscene's track, the mission's lines, the mission-passed tune and the
    ambiences; no radio stations
  - reVC's own gamefiles (its text, frontend and button textures) on top,
    as a reVC install has them

Paths are lowercased; reVC finds files case-insensitively anyway.

usage: pack_assets.py <GTA Vice City folder> <reVC source> <out>
"""
import json
import os
import shutil
import struct
import sys

CUTSCENES = ("tex_3",)  # cuts.img entries to keep (name prefix)
AUDIO = ("sfx.raw", "sfx.sdt", "tex_3.mp3") + tuple("tex3_%d.wav" % i for i in range(1, 9)) + (
    # the mission-passed tune, ambiences (city, beach, water, interiors, silence), the bridge
    "miscom.mp3", "ambsil.mp3", "city.mp3", "water.mp3", "beachamb.mp3", "hcity.mp3", "hbeach.mp3", "hwater.mp3",
    "bclosed.mp3", "bopen.mp3")
SKIP_DIRS = {"audio", "mp3", "movies", "modloader", "cleo", "scripts", "readme", "uninstall", "skins", "mss",
             "icons"}


def lower_walk(root):
    """(relative lowercase path, absolute path) for every file under root."""
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        parts = [] if rel == "." else rel.lower().split(os.sep)
        if parts and parts[0] in SKIP_DIRS:
            dirnames[:] = []
            continue
        if any(p in SKIP_DIRS for p in parts):
            continue
        for f in filenames:
            yield "/".join(parts + [f.lower()]), os.path.join(dirpath, f)


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src) and \
            os.path.getmtime(dst) >= os.path.getmtime(src):
        return
    shutil.copyfile(src, dst)


def find_ci(folder, name):
    for f in os.listdir(folder):
        if f.lower() == name.lower():
            return os.path.join(folder, f)
    sys.exit("missing %s in %s" % (name, folder))


def trim_cuts(game, out):
    """cuts.img/.dir with only the CUTSCENES entries, repacked."""
    anim = find_ci(game, "anim")
    entries = []
    with open(find_ci(anim, "cuts.dir"), "rb") as f:
        d = f.read()
    for i in range(0, len(d), 32):
        off, size = struct.unpack_from("<II", d, i)
        name = d[i + 8:i + 32].split(b"\0")[0].decode("latin1")
        if name.lower().startswith(CUTSCENES):
            entries.append((name, off, size))
    os.makedirs(os.path.join(out, "anim"), exist_ok=True)
    with open(find_ci(anim, "cuts.img"), "rb") as src, \
            open(os.path.join(out, "anim", "cuts.img"), "wb") as img, \
            open(os.path.join(out, "anim", "cuts.dir"), "wb") as dr:
        pos = 0
        for name, off, size in entries:
            src.seek(off * 2048)
            img.write(src.read(size * 2048))
            dr.write(struct.pack("<II", pos, size) + name.encode("latin1").ljust(24, b"\0"))
            pos += size
    return [n for n, _, _ in entries]


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    game, re3, out_root = sys.argv[1:]
    out = os.path.join(out_root, "assets")
    files = {}
    for rel, src in lower_walk(game):
        top = rel.split("/")[0]
        if "/" not in rel:
            continue  # the exe, dlls, readmes
        if rel in ("anim/cuts.img", "anim/cuts.dir"):
            continue
        if top not in ("data", "models", "text", "txd", "anim"):
            continue
        files[rel] = src
    audio = find_ci(game, "audio")
    for name in AUDIO:
        src = find_ci(audio, name)
        files["audio/" + name] = src
    for rel, src in lower_walk(os.path.join(re3, "gamefiles")):
        if rel.endswith(".scm"):
            continue
        files[rel] = src  # reVC's files win, as when installed over the game

    for rel, src in sorted(files.items()):
        copy(src, os.path.join(out, rel))
    kept = trim_cuts(game, out)

    manifest = []
    for dirpath, _, filenames in os.walk(out):
        for f in filenames:
            if f == "manifest.json":
                continue
            p = os.path.join(dirpath, f)
            manifest.append({"path": os.path.relpath(p, out).replace(os.sep, "/"), "size": os.path.getsize(p)})
    manifest.sort(key=lambda e: e["path"])
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump({"files": manifest}, f, indent=0)
    total = sum(e["size"] for e in manifest)
    print("%d files, %.1f MB in %s (cutscene entries: %s)" % (len(manifest), total / 1e6, out, ", ".join(kept)))


if __name__ == "__main__":
    main()
