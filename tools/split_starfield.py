#!/usr/bin/env python3
"""Split one .starfield into N spatial sub-fields, each loadable as its own cloud.

Works on the already-chunked output, so no re-parsing of the source catalogue:
chunks are grouped by direction from the origin (the Sun) and re-emitted with
their payload bytes copied verbatim.
"""
import struct, os, sys, math
import numpy as np

def main(src_index, dst_dir, groups=8):
    base = src_index[:-len('.starfield')]
    d = open(src_index,'rb').read()
    assert d[:4]==b'SFLD'
    ver, nch = struct.unpack_from('<II', d, 4)
    nstars,  = struct.unpack_from('<Q', d, 12)
    nparts,  = struct.unpack_from('<I', d, 20)
    payload = b''.join(open(f'{base}.{p:03d}.part','rb').read() for p in range(nparts))
    poff = [0]*nparts; acc = 0
    for p in range(nparts):
        poff[p] = acc; acc += os.path.getsize(f'{base}.{p:03d}.part')

    meta = []
    off = 24
    for i in range(nch):
        part, o, cnt = struct.unpack_from('<III', d, off)
        x, y, z, ext = struct.unpack_from('<dddf', d, off+12); off += 40
        meta.append((poff[part]+o, cnt, np.array([x,y,z]), ext))

    # Group by sky direction: each cloud is a wedge of sky, so moving one apart
    # separates a coherent region rather than a random scatter.
    cen = np.array([m[2] for m in meta])
    r   = np.linalg.norm(cen, axis=1); r[r<1e-9] = 1e-9
    theta = np.arctan2(cen[:,1], cen[:,0])                  # -pi..pi
    gi = np.floor((theta + math.pi) / (2*math.pi) * groups).astype(int)
    gi = np.clip(gi, 0, groups-1)

    os.makedirs(dst_dir, exist_ok=True)
    name = os.path.basename(base)
    for g in range(groups):
        sel = [i for i in range(nch) if gi[i]==g]
        if not sel: continue
        blob = bytearray(); rows = []
        for i in sel:
            o, cnt, c, ext = meta[i]
            rows.append((0, len(blob), cnt, c, ext))
            blob += payload[o:o+cnt*6]
        gname = f'{name}_{g:02d}'
        open(os.path.join(dst_dir, f'{gname}.000.part'),'wb').write(bytes(blob))
        with open(os.path.join(dst_dir, f'{gname}.starfield'),'wb') as f:
            f.write(b'SFLD'); f.write(struct.pack('<II', 3, len(rows)))
            f.write(struct.pack('<Q', int(sum(rr[2] for rr in rows))))
            f.write(struct.pack('<I', 1))
            for part, o, cnt, c, ext in rows:
                f.write(struct.pack('<III', part, o, cnt))
                f.write(struct.pack('<dddf', float(c[0]), float(c[1]), float(c[2]), ext))
        print(f'  {gname}: {len(rows):4d} chunks, {sum(rr[2] for rr in rows):>9,} stars, {len(blob)/1e6:5.1f} MB')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv)>3 else 8)
