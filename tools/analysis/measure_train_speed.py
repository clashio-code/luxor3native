#!/usr/bin/env python3
"""Measure the original Luxor 3 train speed from recorded gameplay.

The recording is 480x360; the game renders at 800x600, so the video is a
0.6x downscale of the logical frame. Ball blobs are detected by saturation,
matched to the nearest point of the real track polyline read from the shipped
path.LX3TRK, and the arc-length advance per frame gives the speed.

Usage:
  measure_train_speed.py <video.mp4> <map-dir> [--ss 10 --dur 40]
"""
import argparse, glob, os, re, subprocess, sys
import cv2, numpy as np

ROOT = os.environ.get("L3_ROOT", "/sessions/blissful-festive-archimedes/mnt/Luxor 3/Luxor 3")
SCALE = 0.6  # video pixels per logical pixel


def parse_track(path):
    txt = open(path, encoding='latin1').read()
    verts, secs, mode = [], {}, None
    for line in txt.splitlines():
        s = line.strip()
        if not s or s.startswith('//'):
            continue
        if s in ('Verts', 'Sections', 'Reflectors', 'NamedPoints'):
            mode = s; continue
        if s in '{}':
            continue
        if mode == 'Verts':
            q = s.split()
            if len(q) >= 4 and q[0] == 'Vert':
                verts.append((float(q[1]), float(q[2])))
        elif mode == 'Sections':
            m = re.match(r'^"([^"]+)"\s*\{\s*(.*)\s*\}$', s)
            if m:
                secs[m.group(1)] = [int(x) for x in m.group(2).split()]
    return verts, secs


def section_polylines(verts, secs):
    polys = {}
    for name, idx in secs.items():
        pts = [verts[i] for i in idx if 0 <= i < len(verts)]
        if len(pts) < 2:
            continue
        arr = []
        for a, b in zip(pts, pts[1:]):
            ax, ay = a[0]*SCALE, -a[1]*SCALE
            bx, by = b[0]*SCALE, -b[1]*SCALE
            L = np.hypot(bx-ax, by-ay); n = max(1, int(L*3))
            for i in range(n):
                t = i/n
                arr.append((ax+(bx-ax)*t, ay+(by-ay)*t))
        polys[name] = np.array(arr)
    return polys


def detect_balls(path):
    im = cv2.imread(path)
    if im is None:
        return []
    hsv = cv2.cvtColor(im, cv2.COLOR_BGR2HSV)
    s = hsv[..., 1].astype(np.int16); v = hsv[..., 2].astype(np.int16)
    m = ((s > 110) & (v > 110)).astype(np.uint8)
    m[235:, :] = 0     # HUD strip
    m[:, 380:] = 0     # right-hand UI
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((2, 2), np.uint8))
    n, lab, stats, cent = cv2.connectedComponentsWithStats(m, 8)
    out = []
    for i in range(1, n):
        x, y, w, h, a = stats[i]
        if 9 <= w <= 18 and 9 <= h <= 18 and a >= 55:
            out.append((float(cent[i][0]), float(cent[i][1])))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('video')
    ap.add_argument('mapdir')
    ap.add_argument('--ss', type=float, default=10.0)
    ap.add_argument('--dur', type=float, default=40.0)
    ap.add_argument('--work', default='/tmp/l3speed')
    a = ap.parse_args()

    os.makedirs(a.work, exist_ok=True)
    for f in glob.glob(os.path.join(a.work, 'f*.png')):
        os.remove(f)
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-ss', str(a.ss),
                    '-t', str(a.dur), '-i', a.video, '-vf', 'fps=30',
                    os.path.join(a.work, 'f%04d.png'), '-y'], check=True)
    files = sorted(glob.glob(os.path.join(a.work, 'f*.png')))
    print(f'frames: {len(files)}')

    verts, secs = parse_track(os.path.join(ROOT, 'data/maps', a.mapdir, 'path.LX3TRK'))
    polys = section_polylines(verts, secs)
    allP = np.concatenate(list(polys.values()))
    allSec = np.concatenate([[k]*len(v) for k, v in polys.items()])
    # Index of each point inside its own section polyline, so the arc step
    # between two matched points can be measured in section-local space.
    allLocal = np.concatenate([np.arange(len(v)) for v in polys.values()])

    # Per frame, per section: the set of matched blob indices along that
    # section's polyline.
    per_frame = {}
    on_path = 0
    total_blobs = 0
    for k, p in enumerate(files):
        hits = []
        for (cx, cy) in detect_balls(p):
            total_blobs += 1
            d2 = (allP[:, 0]-cx)**2 + (allP[:, 1]-cy)**2
            i = int(np.argmin(d2))
            if d2[i]**0.5 < 10:
                on_path += 1
                hits.append((allSec[i], int(allLocal[i])))
        if hits:
            per_frame[k] = hits
    print(f'blobs: {total_blobs} total, {on_path} on the path '
          f'({100.0*on_path/max(1,total_blobs):.0f}%), {len(per_frame)} frames with hits')

    # Track individual balls: a ball keeps its section while it travels, and
    # its index along that section's polyline advances by the distance it
    # moved. Comparing the same ball across two consecutive frames therefore
    # gives the train speed directly.
    #
    # Each sample also records the ball's position along the section, so the
    # speed can be plotted against position: that shows whether a train
    # starts fast and decelerates, or holds one speed.
    speeds = []
    prev = None
    for k in sorted(per_frame):
        cur = per_frame[k]
        if prev is not None and k == prev[0] + 1:
            for (sec, idx) in cur:
                # Same section, closest previous index.
                cands = [i for (s2, i) in prev[1] if s2 == sec]
                if not cands:
                    continue
                pi = min(cands, key=lambda i: abs(i - idx))
                if abs(pi - idx) > 60:      # not the same ball
                    continue
                poly = polys[sec]
                step = np.hypot(*(poly[idx] - poly[pi]))
                # Position of the ball as a fraction of the section length.
                frac = idx / max(1, len(poly) - 1)
                speeds.append((k, sec, step * 30 / SCALE, frac))
        prev = (k, cur)

    if not speeds:
        print('no train motion measured'); return 1
    vals = np.array([s[2] for s in speeds])
    print(f'samples: {len(vals)}')
    print(f'logical px/s  median={np.median(vals):7.1f}  mean={vals.mean():7.1f}  '
          f'p25={np.percentile(vals,25):7.1f}  p75={np.percentile(vals,75):7.1f}')
    from collections import Counter, defaultdict
    print('section histogram:', Counter(s[1] for s in speeds).most_common(10))
    print('\nper-section speed (logical px/s):')
    by_sec = defaultdict(list)
    for k, sec, v, frac in speeds:
        by_sec[sec].append(v)
    for sec, vs in sorted(by_sec.items(), key=lambda kv: -len(kv[1])):
        arr = np.array(vs)
        print(f'  {sec:14s} n={len(arr):5d}  median={np.median(arr):7.1f}  '
              f'p25={np.percentile(arr,25):7.1f}  p75={np.percentile(arr,75):7.1f}')
    print('\nper-frame samples:')
    for k, sec, v, frac in speeds[::40]:
        print(f'  f{k+1} {sec:12s} {v:7.1f} logical-px/s  frac={frac:.2f}')

    # Speed as a function of position along the section: this shows whether a
    # train holds one speed or decelerates as it crosses a section.
    print('\nspeed vs position within section (median per tenth):')
    by_sec = defaultdict(list)
    for k, sec, v, frac in speeds:
        by_sec[sec].append((frac, v))
    for sec, pts in sorted(by_sec.items(), key=lambda kv: -len(kv[1]))[:4]:
        buckets = defaultdict(list)
        for frac, v in pts:
            buckets[min(9, int(frac*10))].append(v)
        row = ' '.join(f'{np.median(buckets[i]):5.0f}' if buckets.get(i) else '    -'
                       for i in range(10))
        print(f'  {sec:14s} {row}   (tenths of the section, left to right)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
