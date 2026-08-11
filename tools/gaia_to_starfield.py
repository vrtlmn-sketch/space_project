#!/usr/bin/env python3
"""Convert a Gaia Sky octree catalogue into a compact chunked .starfield.

POSITIONS ONLY — no colour, magnitude or size. Six bytes per star.

The renderer needs three things the raw catalogue does not provide:
  * positions in AU stored RELATIVE TO A CHUNK ORIGIN, so they stay precise as
    plain floats and can be uploaded once instead of being re-transformed on the
    CPU every frame (the existing cloud path does the latter, which is why it
    tops out around 250k particles);
  * chunks small enough that frustum culling is meaningful;
  * a shuffled order inside each chunk, so drawing a PREFIX of a chunk is an
    unbiased spatial subsample — that is what makes level-of-detail honest.

Source record layout (reverse-engineered; verified by exact file lengths):
  double x,y,z | float vx,vy,vz | float pmra,pmdec,radvel
  float appmag,absmag | float packedColor | float size
  int64 sourceid | int32 hip | int32 namelen | UTF-16 name
Only the leading three doubles are read.
"""
import struct, glob, os, sys
import numpy as np

IU_KM, AU_KM = 1.0e6, 1.495978707e8
IU_PER_AU    = AU_KM / IU_KM          # 149.5978707
REC          = 80                     # fixed part, before any name
MAX_LEAF     = 8000                   # stars per chunk (finer = better culling)
PART_BYTES   = 8 * 1024 * 1024

def parse_file(path):
    d = open(path, 'rb').read()
    if len(d) < 12: return None
    _tok, _ver, n = struct.unpack('>iii', d[:12])
    if n <= 0: return None
    body = d[12:]
    fixed = n * REC <= len(body)
    if fixed:
        a = np.frombuffer(body[:n*REC], dtype=np.uint8).reshape(n, REC)
        fixed = bool(np.all(a[:, 76:80].copy().view('>i4').ravel() == 0))
    if fixed:
        pos = a[:, 0:24].copy().view('>f8').reshape(n, 3)
    else:   # a named star shifts the stride, so walk this file record by record
        P, off = [], 0
        for _ in range(n):
            if off + REC > len(body): break
            P.append(struct.unpack_from('>ddd', body, off))
            nl = struct.unpack_from('>i', body, off + 76)[0]
            off += REC + max(nl, 0) * 2
        if not P: return None
        pos = np.array(P, dtype=np.float64)
    return pos / IU_PER_AU

def build_chunks(pos, idx, lo, hi, out):
    """Subdivide until a node holds <= MAX_LEAF stars. An octree, not a uniform
    grid: the catalogue is enormously denser near the Sun, and a uniform grid
    would put millions of stars in a handful of cells."""
    if len(idx) == 0: return
    if len(idx) <= MAX_LEAF or np.all(hi - lo < 1e-3):
        out.append((idx, lo.copy(), hi.copy())); return
    mid = (lo + hi) * 0.5
    p = pos[idx]
    for oct_ in range(8):
        m = np.ones(len(idx), bool)
        nlo, nhi = lo.copy(), hi.copy()
        for ax in range(3):
            if oct_ >> ax & 1: m &= p[:, ax] >= mid[ax]; nlo[ax] = mid[ax]
            else:              m &= p[:, ax] <  mid[ax]; nhi[ax] = mid[ax]
        if m.any(): build_chunks(pos, idx[m], nlo, nhi, out)

def main(src, dst_dir, name):
    files = sorted(glob.glob(os.path.join(src, 'particles', '*.bin')))
    print(f'reading {len(files)} particle files ...')
    PS = []
    for i, f in enumerate(files):
        r = parse_file(f)
        if r is not None: PS.append(r)
        if (i + 1) % 300 == 0:
            print(f'  {i+1}/{len(files)}  {sum(len(p) for p in PS):,} stars')
    pos = np.concatenate(PS); del PS
    print(f'parsed {len(pos):,} stars')

    lo, hi = pos.min(0), pos.max(0)
    c, half = (lo + hi) * 0.5, (hi - lo).max() * 0.5 * 1.001
    print(f'extent AU: {lo} .. {hi}')
    chunks = []
    build_chunks(pos, np.arange(len(pos)), c - half, c + half, chunks)
    print(f'{len(chunks)} chunks (<= {MAX_LEAF} stars each)')

    os.makedirs(dst_dir, exist_ok=True)
    rng = np.random.default_rng(12345)
    meta, parts, cur = [], [], bytearray()
    for idx, clo, chi in chunks:
        idx = idx.copy(); rng.shuffle(idx)      # prefix of a chunk = unbiased sample
        # TIGHT bounds around the stars actually present, not the octree cell.
        # A sparse outer cell is an enormous box holding a handful of stars in
        # one corner: its bounding sphere clips the view frustum constantly, so
        # the renderer kept spending its budget on chunks that produced no
        # pixels. Tight bounds also shrink the quantisation step.
        p = pos[idx]
        plo, phi = p.min(0), p.max(0)
        ccen = (plo + phi) * 0.5
        cext = float(np.max(phi - plo)) * 0.5
        if cext <= 0: cext = 1.0
        q = np.clip(np.rint((pos[idx] - ccen) / cext * 32767.0), -32767, 32767).astype('<i2')
        b = q.tobytes()                          # 3 x int16 = 6 bytes per star
        if len(cur) + len(b) > PART_BYTES and cur:
            parts.append(bytes(cur)); cur = bytearray()
        meta.append((len(parts), len(cur), len(idx), ccen, cext))
        cur += b
    if cur: parts.append(bytes(cur))

    for i, p in enumerate(parts):
        open(os.path.join(dst_dir, f'{name}.{i:03d}.part'), 'wb').write(p)

    with open(os.path.join(dst_dir, f'{name}.starfield'), 'wb') as f:
        f.write(b'SFLD')
        f.write(struct.pack('<II', 3, len(meta)))          # version 3 = positions only
        f.write(struct.pack('<Q', int(sum(m[2] for m in meta))))
        f.write(struct.pack('<I', len(parts)))
        for part, off, cnt, cen, ext in meta:               # 40 bytes per chunk
            f.write(struct.pack('<III', part, off, cnt))
            f.write(struct.pack('<dddf', float(cen[0]), float(cen[1]), float(cen[2]), ext))

    tot = sum(len(p) for p in parts); stars = sum(m[2] for m in meta)
    print(f'\nwrote {len(parts)} part files, {tot/1e6:.1f} MB + index')
    print(f'{stars:,} stars @ {tot/max(stars,1):.2f} B/star')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else 'universe')
