#!/usr/bin/env python3
"""Reference parser for the Luxor 3 data formats.

Everything under data/ is plain text: '//' line comments, '{ }' blocks, and
entries that are either "key value", "key { ... }" or a bare list of values.
A single grammar therefore covers .lvl, .LX3TRK, flavors*.txt, treasure*.txt,
survivalData.txt and AnimDefs.txt.

Line-oriented rules (what the original data actually uses):
  * one entry per line; a line holds either
        key value            -> key/value
        key v1, v2           -> key with a list of values
        key                  -> key whose '{ ... }' block follows
        value                -> bare value (list)
  * a block whose entries are all bare values is a list
  * a line holding only numbers is always a bare value list

Usage
-----
  python3 luxor_parse.py validate <game_dir>     # resolve every reference
  python3 luxor_parse.py track   <path.LX3TRK>
  python3 luxor_parse.py level   <file.lvl>
  python3 luxor_parse.py animdefs <AnimDefs.txt>
"""
import os, re, sys, json, math

_num_re = re.compile(r'^[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?$')
def _isnum(s): return bool(_num_re.match(str(s)))

class Block:
    """A '{ ... }' body: ordered key/value pairs plus any bare values."""
    __slots__ = ("pairs", "bare")
    def __init__(self, pairs=None, bare=None):
        self.pairs = pairs or []
        self.bare = bare or []
    def get(self, key, default=None):
        for k, v in self.pairs:
            if k == key: return v
        return default
    def all(self, key):
        return [v for k, v in self.pairs if k == key]
    def kv(self):
        return dict(self.pairs)
    def __repr__(self):
        return f"Block(pairs={len(self.pairs)}, bare={len(self.bare)})"

# ------------------------------------------------------------------ lexer
class Tok:
    __slots__ = ("kind", "val", "line")
    def __init__(self, kind, val, line): self.kind, self.val, self.line = kind, val, line
    def __repr__(self): return f"{self.kind}:{self.val!r}@{self.line}"

def lex(text):
    toks, i, line, n = [], 0, 1, len(text)
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1; i += 1; continue
        if c in ' \t\r':
            i += 1; continue
        if text.startswith('//', i):
            j = text.find('\n', i); i = n if j < 0 else j; continue
        if c == '"':
            j = text.find('"', i + 1)
            if j < 0: j = n
            toks.append(Tok('str', text[i+1:j], line)); i = j + 1; continue
        if c in '{}(),':
            toks.append(Tok(c, c, line)); i += 1; continue
        m = re.match(r'[^\s{}(),"]+', text[i:])
        if m:
            toks.append(Tok('word', m.group(0), line)); i += len(m.group(0)); continue
        i += 1
    return toks

def _val(tok):
    if tok.kind == 'word' and _isnum(tok.val):
        f = float(tok.val)
        return int(f) if f.is_integer() and '.' not in tok.val else f
    return tok.val

# ------------------------------------------------------------------ parser
class P:
    def __init__(self, toks): self.t = toks; self.i = 0
    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else None
    def next(self):
        t = self.peek()
        if t is not None: self.i += 1
        return t

    def line_words(self):
        """Consume the value tokens on the current line (commas skipped)."""
        row = self.peek().line
        words = []
        while True:
            t = self.peek()
            if t is None or t.line != row: break
            if t.kind == ',': self.next(); continue
            if t.kind in ('word', 'str'):
                words.append(_val(self.next())); continue
            break
        return words

    def parse_tuple(self):
        assert self.peek().kind == '('
        self.next()
        vals = []
        while True:
            t = self.peek()
            if t is None: break
            if t.kind == ')': self.next(); break
            if t.kind == ',': self.next(); continue
            if t.kind == '(':
                vals.append(self.parse_tuple()); continue
            vals.append(_val(self.next()))
        return vals

    def parse_block(self):
        """Current token is '{'. Returns a Block."""
        assert self.peek() and self.peek().kind == '{', f"expected {{ at {self.peek()}"
        self.next()
        pairs, bare = [], []
        while True:
            t = self.peek()
            if t is None: break
            if t.kind == '}': self.next(); break
            if t.kind == ',': self.next(); continue
            if t.kind == '{':
                raise SyntaxError(f"unexpected {{ at line {t.line}")
            row = t.line
            words = self.line_words()
            if not words:
                self.next(); continue
            nxt = self.peek()
            if nxt is not None and nxt.kind == '{':
                key = " ".join(str(w) for w in words)
                pairs.append((key, self.parse_block()))
            elif nxt is not None and nxt.kind == '(':
                key = str(words[0])
                pairs.append((key, self.parse_tuple()))
            elif len(words) == 1:
                bare.append(words[0])
            elif all(isinstance(w, (int, float)) and not isinstance(w, bool) for w in words):
                bare.extend(words)
            else:
                pairs.append((str(words[0]), words[1] if len(words) == 2 else words[1:]))
        return Block(pairs, bare)

def parse_doc(text):
    """Top level: a sequence of entries with no enclosing braces."""
    p = P(lex(text))
    pairs, bare = [], []
    while True:
        t = p.peek()
        if t is None: break
        if t.kind == ',': p.next(); continue
        if t.kind == '}': p.next(); continue
        if t.kind == '{':
            raise SyntaxError(f"unexpected {{ at line {t.line}")
        words = p.line_words()
        if not words:
            p.next(); continue
        nxt = p.peek()
        if nxt is not None and nxt.kind == '{':
            key = " ".join(str(w) for w in words)
            pairs.append((key, p.parse_block()))
        elif nxt is not None and nxt.kind == '(':
            pairs.append((str(words[0]), p.parse_tuple()))
        elif len(words) == 1:
            bare.append(words[0])
        elif all(isinstance(w, (int, float)) and not isinstance(w, bool) for w in words):
            bare.extend(words)
        else:
            pairs.append((str(words[0]), words[1] if len(words) == 2 else words[1:]))
    return Block(pairs, bare)

# ------------------------------------------------------------------ helpers
def as_list(v):
    if v is None: return []
    if isinstance(v, list): return list(v)
    return [v]

# ------------------------------------------------------------------ include
def resolve_includes(path, seen=None):
    """Textual #include expansion; includes resolve relative to the including
    file's own directory (matches Luxor's behaviour)."""
    seen = seen if seen is not None else set()
    real = os.path.realpath(path)
    if real in seen:
        return ""
    seen.add(real)
    out, base = [], os.path.dirname(path)
    with open(path, encoding='latin1') as f:
        for line in f:
            m = re.match(r'\s*#include\s+"([^"]+)"', line)
            if m:
                out.append(resolve_includes(os.path.join(base, m.group(1)), seen))
            else:
                out.append(line)
    return "\n".join(out)

# ------------------------------------------------------------------ track
def parse_track(path):
    doc = parse_doc(open(path, encoding='latin1').read())
    verts = []
    for blk in doc.all('Verts'):
        for k, v in blk.pairs:
            if k == 'Vert' and isinstance(v, list) and len(v) >= 2:
                verts.append((float(v[0]), float(v[1])))
    sections = {}
    for blk in doc.all('Sections'):
        for name, val in blk.pairs:
            if isinstance(val, Block):
                idxs = []
                for k2, v2 in val.pairs:
                    idxs += [int(x) for x in as_list(v2)]
                idxs += [int(x) for x in val.bare]
                sections[name] = idxs
            elif isinstance(val, list):
                sections[name] = [int(x) for x in val]
            elif isinstance(val, (int, float)):
                sections[name] = [int(val)]
    named = {}
    for blk in doc.all('NamedPoints'):
        for name, pt in blk.pairs:
            if isinstance(pt, list) and len(pt) >= 2:
                named[name] = (float(pt[0]), float(pt[1]))
    reflectors = []
    for blk in doc.all('Reflectors'):
        reflectors.extend(blk.pairs)
    return {"path": path, "verts": verts, "sections": sections,
            "named": named, "reflectors": reflectors}

def section_polyline(track, name):
    idx = track["sections"].get(name)
    if idx is None:
        for k, v in track["sections"].items():
            if k.lower() == name.lower(): idx = v; break
    if idx is None: return None
    return [track["verts"][i] for i in idx if 0 <= i < len(track["verts"])]

def polyline_len(pts):
    return sum(math.dist(pts[i], pts[i+1]) for i in range(len(pts)-1))

def point_at(pts, s):
    """Arc-length position along a polyline; s in pixels."""
    if not pts: return None
    for i in range(len(pts)-1):
        seg = math.dist(pts[i], pts[i+1])
        if s <= seg or i == len(pts)-2:
            t = 0.0 if seg == 0 else min(max(s/seg, 0.0), 1.0)
            return (pts[i][0] + (pts[i+1][0]-pts[i][0])*t,
                    pts[i][1] + (pts[i+1][1]-pts[i][1])*t)
        s -= seg
    return pts[-1]

def section_lengths(track):
    return {k: polyline_len(section_polyline(track, k)) for k in track["sections"]}

# ------------------------------------------------------------------ level
def parse_level(path):
    raw = open(path, encoding='latin1').read()
    doc = parse_doc(resolve_includes(path))
    lv = {"path": path,
          "includes": re.findall(r'#include\s+"([^"]+)"', raw)}
    lv["MapPath"]          = doc.get("MapPath")
    lv["LevelDisplayName"] = doc.get("LevelDisplayName")
    lv["LevelMusic"]       = doc.get("LevelMusic")
    lv["MenuMusic"]        = doc.get("MenuMusic")
    lv["BonusTime"]        = doc.get("BonusTime")
    lv["NumTrainsToSpawn"] = doc.get("NumTrainsToSpawn")
    tcd = doc.get("TrainConstructionData")
    lv["TrainConstructionData"] = tcd.kv() if isinstance(tcd, Block) else {}
    lv["StartingTrackSectionSpawnOrder"] = []
    for blk in doc.all('StartingTrackSectionSpawnOrder'):
        lv["StartingTrackSectionSpawnOrder"] += list(blk.bare)
    lv["TrainTrack"] = [b.kv() for b in doc.all('TrainTrack')]
    lv["Train"] = []
    for b in doc.all('Train'):
        d = b.kv()
        fl = b.get('FlavorList')
        d["_flavors"] = list(fl.bare) if isinstance(fl, Block) else as_list(fl)
        lv["Train"].append(d)
    lv["WinTrain"] = [b.kv() for b in doc.all('WinTrain')]
    lv["TrackSections_referenced"] = (
        [s for s in as_list(doc.get('TrackSection'))] +
        [t for b in doc.all('Train') for t in as_list(b.get('TrackSegment'))])
    fl = doc.get('AllowableGamePieceFlavors')
    lv["AllowableGamePieceFlavors"] = {}
    if isinstance(fl, Block):
        for diff, sub in fl.pairs:
            lv["AllowableGamePieceFlavors"][diff] = (list(sub.bare) if isinstance(sub, Block)
                                                     else as_list(sub))
    tr = doc.get('AllowableTreasureFlavors')
    lv["AllowableTreasureFlavors"] = (list(tr.bare) if isinstance(tr, Block) else as_list(tr))
    am = doc.get('Ammo')
    lv["Ammo"] = list(am.bare) if isinstance(am, Block) else as_list(am)
    if lv["TrainTrack"]:                 lv["mode"] = "crossingTheNile"
    elif lv["WinTrain"] and lv["Train"]: lv["mode"] = "centipede"
    elif lv["Train"]:                    lv["mode"] = "puzzle"
    elif lv["StartingTrackSectionSpawnOrder"]: lv["mode"] = "classic"
    else:                                lv["mode"] = "unknown"
    return lv

# ------------------------------------------------------------------ animdefs
def parse_animdefs(path):
    doc = parse_doc(open(path, encoding='latin1').read())
    out = []
    for b in doc.all("Sequence"):
        d = b.kv()
        d["_fileNames"] = [v for k, v in b.pairs if k.lower() == "filename"]
        out.append(d)
    return out

# ------------------------------------------------------------------ validate
def validate(game_dir):
    data = os.path.join(game_dir, "data")
    maps_dir, lvl_dir = os.path.join(data, "maps"), os.path.join(data, "levels")
    rep = {"track_refs_ok": 0, "track_refs_bad": [], "include_ok": 0, "include_bad": [],
           "maps": {}, "levels": {}, "mode_counts": {}}

    tracks = {}
    for m in sorted(os.listdir(maps_dir)):
        p = os.path.join(maps_dir, m, "path.LX3TRK")
        if os.path.isfile(p):
            t = parse_track(p)
            tracks[m.lower()] = t
            rep["maps"][m] = {"verts": len(t["verts"]), "sections": len(t["sections"]),
                              "named_points": len(t["named"])}

    for dp, dn, fn in os.walk(lvl_dir):
        for f in sorted(fn):
            if not f.endswith(".lvl"): continue
            full = os.path.join(dp, f)
            for inc in re.findall(r'#include\s+"([^"]+)"', open(full, encoding='latin1').read()):
                if os.path.isfile(os.path.join(dp, inc)): rep["include_ok"] += 1
                else: rep["include_bad"].append(f"{f} -> {inc}")
            lv = parse_level(full)
            key = (lv["MapPath"] or "").replace("data/maps/", "").strip("/").lower()
            trk = tracks.get(key)
            if trk is None:
                if lv["MapPath"]:
                    rep["track_refs_bad"].append(f"{f}: map '{lv['MapPath']}' has no path.LX3TRK")
            else:
                names = {k.lower() for k in trk["sections"]}
                for sname in (lv["StartingTrackSectionSpawnOrder"] +
                              lv["TrackSections_referenced"]):
                    if sname.lower() in names: rep["track_refs_ok"] += 1
                    else: rep["track_refs_bad"].append(f"{f}: section '{sname}' missing in {key}")
            rel = os.path.relpath(full, lvl_dir).replace("\\", "/")
            rep["levels"][rel] = {"mode": lv["mode"], "map": key,
                                  "sections_used": sorted(set(
                                      lv["StartingTrackSectionSpawnOrder"] +
                                      lv["TrackSections_referenced"]))}
            rep["mode_counts"][lv["mode"]] = rep["mode_counts"].get(lv["mode"], 0) + 1
    return rep

# ------------------------------------------------------------------ CLI
if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    cmd, arg = sys.argv[1], sys.argv[2]
    if cmd == "validate":
        print(json.dumps(validate(arg), indent=2))
    elif cmd == "track":
        t = parse_track(arg)
        print(f"verts={len(t['verts'])} sections={len(t['sections'])} named={len(t['named'])}")
        for k, v in t["sections"].items():
            pts = section_polyline(t, k)
            print(f"  {k:20s} {len(v):4d} verts  len={polyline_len(pts):8.1f}px")
    elif cmd == "level":
        print(json.dumps(parse_level(arg), indent=2, default=str))
    elif cmd == "animdefs":
        for s in parse_animdefs(arg):
            print(f"{str(s.get('name')):24s} {s.get('_fileNames')} @{s.get('playbackRate')} "
                  f"loop={s.get('isLooping')} w={s.get('weight')}")
