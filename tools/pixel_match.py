import numpy as np
from PIL import Image
def load(p): return np.asarray(Image.open(p).convert('RGB'),dtype=np.float32)/255.0
rt=load('/tmp/cmp_rt.png'); ra=load('/tmp/cmp_raster.png')
H,W,_=ra.shape
def lum(im): return 0.299*im[...,0]+0.587*im[...,1]+0.114*im[...,2]
Lrt,Lra=lum(rt),lum(ra)
def iou(a,b): 
    u=(a|b).sum(); return (a&b).sum()/u if u else 0.0
# 1) STAR alignment: bright compact pixels
star_rt=Lrt>0.6; star_ra=Lra>0.6
# 2) DUST alignment: warm + mid luminance
def dust(im,L): R,B=im[...,0],im[...,2]; return ((R-B)>0.10)&(L>0.05)&(L<0.8)
d_rt,d_ra=dust(rt,Lrt),dust(ra,Lra)
# 3) global luminance correlation
m=(Lra>0.02)|(Lrt>0.02)
corr=np.corrcoef(Lrt[m],Lra[m])[0,1]
# 4) windowed SSIM (downsample 4x, simple global)
def ds(x,k=4): 
    h,w=x.shape; h,w=h//k*k,w//k*k; return x[:h,:w].reshape(h//k,k,w//k,k).mean((1,3))
a,b=ds(Lrt),ds(Lra)
mua,mub=a.mean(),b.mean(); va,vb=a.var(),b.var(); cov=((a-mua)*(b-mub)).mean()
c1,c2=0.01**2,0.03**2
ssim=((2*mua*mub+c1)*(2*cov+c2))/((mua**2+mub**2+c1)*(va+vb+c2))
print(f"STAR-mask  IoU = {iou(star_rt,star_ra):.3f}   (RT {star_rt.mean()*100:.1f}%  RA {star_ra.mean()*100:.1f}%)")
print(f"DUST-mask  IoU = {iou(d_rt,d_ra):.3f}   (RT {d_rt.mean()*100:.1f}%  RA {d_ra.mean()*100:.1f}%)")
print(f"Luminance correlation = {corr:.3f}   (1.0 = identical structure)")
print(f"Global SSIM = {ssim:.3f}")
# difference heatmap
diff=np.clip(np.abs(rt-ra)*2.0,0,1)
Image.fromarray((diff*255).astype(np.uint8)).save('/tmp/cmp_diff.png')
print("wrote /tmp/cmp_diff.png (|RT - raster| ×2)")
