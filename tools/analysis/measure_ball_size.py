#!/usr/bin/env python3
"""Measure the on-screen size of a Luxor 3 ball sprite in recorded gameplay.

The game data stores ball frames as 32x32 PNGs. The recording is 480x360 while
the game renders in an 800x600 logical frame, so the sprite's apparent size
tells us the recording's scale factor: 32*scale for a downscaled capture,
32 for a native 480x360 capture.
"""
import glob, os, subprocess, sys
import cv2, numpy as np

def main():
    video, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    for f in glob.glob(os.path.join(work, 'f*.png')):
        os.remove(f)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-ss','12','-t','6',
                    '-i',video,'-vf','fps=30',os.path.join(work,'f%04d.png'),'-y'],check=True)
    files = sorted(glob.glob(os.path.join(work,'f*.png')))
    widths = []
    for p in files:
        im = cv2.imread(p); hsv = cv2.cvtColor(im, cv2.COLOR_BGR2HSV)
        s = hsv[...,1].astype(np.int16); v = hsv[...,2].astype(np.int16)
        m = ((s>110)&(v>110)).astype(np.uint8)
        m[235:,:]=0; m[:,380:]=0
        m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((2,2),np.uint8))
        n,lab,stats,cent = cv2.connectedComponentsWithStats(m,8)
        for i in range(1,n):
            x,y,w,h,a = stats[i]
            if a>=55 and 4<=w<=40 and 4<=h<=40:
                widths.append((w,h,a))
    if not widths:
        print('no balls detected'); return 1
    ws = np.array([w for w,h,a in widths])
    hs = np.array([h for w,h,a in widths])
    print(f'ball blobs: {len(ws)}')
    print(f'width  median={np.median(ws):.1f} p25={np.percentile(ws,25):.1f} p75={np.percentile(ws,75):.1f} max={ws.max()}')
    print(f'height median={np.median(hs):.1f} p25={np.percentile(hs,25):.1f} p75={np.percentile(hs,75):.1f} max={hs.max()}')
    # The sprite PNG is 32x32 including its transparent margin; the coloured
    # body is smaller. Measure the real body size from the game data.
    root = os.environ.get('L3_ROOT','/sessions/blissful-festive-archimedes/mnt/Luxor 3/Luxor 3')
    src = os.path.join(root,'data/gfx/balls/red/ball0004.png')
    img = cv2.imread(src, cv2.IMREAD_UNCHANGED)
    alpha = img[...,3]
    ys,xs = np.where(alpha>16)
    body = xs.max()-xs.min()+1
    print(f'data sprite: png {img.shape[1]}x{img.shape[0]}, opaque body {body}px')
    print(f'implied scale from body: {np.median(ws)/body:.3f} (0.6 => downscaled 800x600 capture)')
    return 0

if __name__ == '__main__':
    sys.exit(main())
