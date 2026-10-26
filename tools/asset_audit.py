#!/usr/bin/env python3
"""Audit every asset reference in Luxor 3's data/ tree.

Checks that each reference from a config file resolves to a real file on disk:
  * AnimDefs.txt      fileName / fileNamePlayBackwards -> frame families
  * *.effect          Texture -> PNG/JPG
  * Placeables.txt    effect -> data/effects/<name>.effect
  * *.txt             texture / shadowTexture / Icon -> image
  * GameSounds.txt    file -> OGG
  * locale strings    "#Key" -> English.txt

Frame families: an animation reference such as "data/gfx/balls/red/ball"
resolves to ball0000.png..ball0029.png, so a reference is satisfied when any
file starts with that prefix.

Usage: python3 asset_audit.py <game_dir>
"""
import os, re, sys, collections

EXT_PROBES = ['.png', '.jpg', '.ogg', '.effect', '.txt', '.fontdef']


def main(game_dir):
    base = game_dir
    data = os.path.join(base, 'data')
    files = set()
    for dp, dn, fn in os.walk(base):
        for f in fn:
            rel = os.path.relpath(os.path.join(dp, f), base).replace('\\', '/')
            files.add(rel)
    lower = {f.lower() for f in files}

    def exists(path, root=None):
        p = path.replace('\\', '/').lstrip('/')
        if root and not p.lower().startswith(root.lower()):
            p = root.rstrip('/') + '/' + p
        if p.lower() in lower:
            return True
        return any((p + e).lower() in lower for e in EXT_PROBES)

    def family(prefix):
        pref = prefix.replace('\\', '/').lower()
        return [f for f in files if f.lower().startswith(pref)]

    out = []
    bad_total = 0

    # ---- AnimDefs -------------------------------------------------------
    anim_path = os.path.join(data, 'AnimDefs.txt')
    txt = re.sub(r'//.*', '', open(anim_path, encoding='latin1').read())
    refs = sorted(set(m.group(1) for m in re.finditer(
        r'\bfile(?:Name|NamePlayBackwards)\s+"([^"]+)"', txt, re.I)))
    direct = fam = miss = 0
    for r in refs:
        if exists(r):
            direct += 1; continue
        if family(r):
            fam += 1
        else:
            miss += 1
            out.append(f"  MISSING animation frames: {r}")
    out.append(f"AnimDefs      : {len(refs)} refs | direct={direct} | frame-family={fam} | MISSING={miss}")
    bad_total += miss

    # ---- .effect textures ----------------------------------------------
    eff_total = eff_bad = 0
    effdir = os.path.join(data, 'effects')
    for f in sorted(os.listdir(effdir)):
        if not f.endswith('.effect'): continue
        t = re.sub(r'//.*', '', open(os.path.join(effdir, f), encoding='latin1').read())
        for m in re.finditer(r'\bTexture\s+"([^"]+)"', t):
            eff_total += 1
            if not exists(m.group(1)):
                eff_bad += 1
                out.append(f"  MISSING texture: {f} -> {m.group(1)}")
    out.append(f".effect       : {eff_total} texture refs | MISSING={eff_bad}")
    bad_total += eff_bad

    # ---- placeables ----------------------------------------------------
    pl_total = pl_bad = 0
    mapsdir = os.path.join(data, 'maps')
    for dp, dn, fn in os.walk(mapsdir):
        for f in fn:
            if f != 'Placeables.txt': continue
            t = re.sub(r'//.*', '', open(os.path.join(dp, f), encoding='latin1').read())
            for m in re.finditer(r'\beffect\s+"([^"]+)"', t):
                pl_total += 1
                if not exists('data/effects/' + m.group(1) + '.effect'):
                    pl_bad += 1
                    out.append(f"  MISSING effect: {f} -> {m.group(1)}")
    out.append(f"Placeables    : {pl_total} effect refs | MISSING={pl_bad}")
    bad_total += pl_bad

    # ---- UI textures ---------------------------------------------------
    ui_total = ui_bad = 0
    for dp, dn, fn in os.walk(data):
        for f in fn:
            if not f.endswith('.txt'): continue
            p = os.path.join(dp, f)
            t = re.sub(r'//.*', '', open(p, encoding='latin1').read())
            for m in re.finditer(r'\b(?:texture|shadowTexture|Icon)\s+"([^"]+)"', t, re.I):
                ui_total += 1
                if not exists(m.group(1)):
                    ui_bad += 1
                    out.append(f"  MISSING texture: {os.path.relpath(p, base)} -> {m.group(1)}")
    out.append(f"UI textures   : {ui_total} refs | MISSING={ui_bad}")
    bad_total += ui_bad

    # ---- sounds ---------------------------------------------------------
    s_total = s_bad = 0
    t = re.sub(r'//.*', '', open(os.path.join(data, 'GameSounds.txt'), encoding='latin1').read())
    for m in re.finditer(r'\bfile\s+"([^"]+)"', t):
        s_total += 1
        if not exists(m.group(1), root='data/sound'):
            s_bad += 1
            out.append(f"  MISSING sound: {m.group(1)}")
    out.append(f"GameSounds    : {s_total} file refs | MISSING={s_bad}")
    bad_total += s_bad

    # ---- localization ---------------------------------------------------
    loc = set(re.findall(r'^#([A-Za-z0-9_]+)',
                          open(os.path.join(data, 'locale/strings/English.txt'),
                               encoding='latin1').read(), re.M))
    str_total = str_bad = 0
    for dp, dn, fn in os.walk(data):
        for f in fn:
            if not f.endswith('.txt'): continue
            p = os.path.join(dp, f)
            if 'locale' in p: continue
            t = re.sub(r'//.*', '', open(p, encoding='latin1').read())
            for m in re.finditer(r'"#([A-Za-z0-9_]+)"', t):
                str_total += 1
                if m.group(1) not in loc:
                    str_bad += 1
                    out.append(f"  MISSING locale key: {os.path.relpath(p, base)} -> #{m.group(1)}")
    out.append(f"Localization  : {str_total} key refs | MISSING={str_bad} "
               f"| English.txt defines {len(loc)} keys")
    bad_total += str_bad

    out.append("")
    out.append(f"TOTAL UNRESOLVED REFERENCES: {bad_total}")
    print("\n".join(out))
    return 0 if bad_total == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
