import numpy as np
from PIL import Image
def load(p): return np.asarray(Image.open(p).convert('RGB'),dtype=np.float32)/255.0
rt=load('/tmp/cmp_rt.png'); ra=load('/tmp/cmp_raster.png')
H,W,_=ra.shape
def dustmask(img):
    R,G,B=img[...,0],img[...,1],img[...,2]; lum=0.299*R+0.587*G+0.114*B
    return ((R-B)>0.06)&(lum>0.04)&(lum<0.85)
# horizontal profile of dust density across the band (12 columns, x 8%..62%)
def profile(img,name):
    m=dustmask(img); x0,x1=int(W*0.08),int(W*0.62)
    cols=np.linspace(x0,x1,13).astype(int)
    frac=[m[:,cols[i]:cols[i+1]].mean()*100 for i in range(12)]
    print(name.ljust(8),"dust%% per column L→R:", " ".join(f"{f:4.0f}" for f in frac))
    # vertical concentration: std of dust y-positions (how tight the band is)
    ys,xs=np.where(m[:, x0:x1]); 
    print("        dust y-spread(std px)=%.0f  centroid_y=%.0f  count=%d"%(ys.std(),ys.mean(),len(ys)))
profile(ra,"RASTER"); profile(rt,"RT")
