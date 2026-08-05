import numpy as np
from PIL import Image
import sys

def load(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=np.float32)/255.0

rt = load('/tmp/cmp_rt.png'); ra = load('/tmp/cmp_raster.png')
H,W,_ = ra.shape
# Central band region (avoid Earth on the right third, focus on the galaxy band rows)
x0,x1 = int(W*0.10), int(W*0.62)
y0,y1 = int(H*0.10), int(H*0.60)
def stats(img, name):
    reg = img[y0:y1, x0:x1]
    R,G,B = reg[...,0], reg[...,1], reg[...,2]
    lum = 0.299*R+0.587*G+0.114*B
    warm = R - B                          # reddening indicator
    # "dust" pixels: warm (orange/red), not pure-white star, not black sky
    dust = (warm > 0.06) & (lum > 0.04) & (lum < 0.85)
    print(f"== {name} (central band) ==")
    print(f"  mean RGB   = ({R.mean():.3f}, {G.mean():.3f}, {B.mean():.3f})")
    print(f"  mean warmth(R-B) = {warm.mean():+.4f}   (higher = more reddening)")
    print(f"  mean lum   = {lum.mean():.3f}")
    print(f"  dust-pixel fraction = {dust.mean()*100:.1f}%   (warm reddened pixels)")
    print(f"  mean warmth WITHIN dust = {warm[dust].mean():+.3f}" if dust.any() else "  (no dust pixels)")
    print(f"  red energy / blue energy = {R.sum()/max(B.sum(),1):.3f}")
stats(ra, "RASTER (target)")
stats(rt, "RT")
