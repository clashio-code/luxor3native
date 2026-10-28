#!/usr/bin/env python3
"""Calibrate the recording's scale from the spacing of balls in a train.

Adjacent balls in a Luxor 3 train are drawn touching, so the distance between
their centres equals the ball diameter in logical pixels (the sprite PNG is
32x32). Measuring that distance in the video gives the recording's scale
factor without assuming one.
"""
import glob, os, subprocess, sys
import cv2, numpy as np

def blobs(p):
    im = cv2.imread(p); hsv = cv2.cvtColor(im, cv2.COLOR_BGR2HSV)
    s = hsv[...,1].astype(np.int16); v = hsv[...,2].astype(np.int16)
    m = ((s>110)&(v>110)).astype(np.uint8)
    m[235:,:]=0; m[:,380:]=0
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((2,2),np.uint8))
    n,lab,stats,cent = cv2.connectedComponentsWithStats(m,8)
    out=[]
    for i in range(1,n):
        x,y,w,h,a = stats[i]
        if a>=55 and 6<=w<=26 and 6<=h<=26:
            out.append((float(cent[i][0]),float(cent[i][1]),int(w)))
    return out

def main():
    video, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    for f in glob.glob(os.path.join(work,'f*.png')): os.remove(f)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-ss','12','-t','6',
                    '-i',video,'-vf','fps=30',os.path.join(work,'f%04d.png'),'-y'],check=True)
    files = sorted(glob.glob(os.path.join(work,'f*.png')))
    gaps = []
    for p in files:
        bs = blobs(p)
        for i,(x1,y1,w1) in enumerate(bs):
            for (x2,y2,w2) in bs[i+1:]:
                d = np.hypot(x2-x1, y2-y1)
                if 6 <= d <= 30:   # adjacent balls in a chain
                    gaps.append(d)
    if not gaps:
        print('no adjacent-ball pairs found'); return 1
    g = np.array(gaps)
    # The true spacing shows up as the mode of the distribution; pixel
    # quantisation puts neighbours at +-1 of it.
    hist, edges = np.histogram(g, bins=np.arange(5,32,1.0))
    top = int(np.argmax(hist))
    mode = 0.5*(edges[top]+edges[top+1])
    print(f'pairs: {len(g)}')
    print(f'gap median={np.median(g):.1f}  mode={mode:.1f} video-px')
    for i in range(len(hist)):
        if hist[i] > len(g)*0.02:
            print(f'   gap {edges[i]:.1f}-{edges[i+1]:.1f}: {hist[i]}')
    print(f'\nimplied scale (ball diameter 32 logical px): {mode/32:.3f}')
    print('  0.600 => the recording is a 0.6x downscale of the 800x600 frame')
    return 0

if __name__ == '__main__':
    sys.exit(main())
