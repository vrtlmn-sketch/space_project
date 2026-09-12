#version 460 core
out vec4 FragColor;

// One step of the auto-exposure reduction. Every level carries three things:
//   R  the EXACT mean luminance of the pixels below this texel
//   G  how many real pixels that is (so an edge texel covering fewer weighs less)
//   B  the brightest REGION of at least the highlight size found below it
//
// Why R alone was not enough: a small but blazing object (Saturn at half a
// percent of the frame, a nearby Sun) barely moves the mean, so exposure stayed
// high and it blew out — the classic "white Moon on a black sky" failure.
// Why not simply the brightest pixels: in a frame of stars those ARE star cores,
// points that are supposed to saturate. SIZE is what tells them apart. A star
// averaged over a block of the highlight size spreads to almost nothing; a
// planet or a near Sun that fills the block keeps its full brightness.
//
// B is not the single brightest region any more but the POWER MEAN of region
// brightnesses, (mean of region^p)^(1/p), weighted by area. With the maximum,
// one planet corner at the frame edge filled one region at full brightness,
// darkened the whole frame, and flipped it back in one frame when it slid off.
// A planet filling the frame still reads at full brightness; a corner covering a
// few regions out of thousands barely counts. B carries the mean of region^p;
// aeExposureFrag takes the p-th root.
//
// The region is a SLIDING window, not a fixed grid: on a grid an object split
// across four blocks reads up to 4x dimmer than the same object centred in one,
// so its brightness would jump as it drifted — flicker. At the highlight level
// (uHiMode 1) every 2x2 group of source texels starting inside this texel is
// tried, including groups reaching into the next texel, and the brightest kept.
// Above it (uHiMode 2) the area-weighted mean of region^p is carried up; below it (0) B is unused.
//
// glGenerateMipmap cannot be used for R: on non-power-of-two sizes each halving
// drops the odd last row or column, so the "mean" silently left out part of the
// frame. Here sizes round UP and nothing is dropped.
uniform sampler2D uTexture;  // pass 0: the HDR scene; later: the previous level
uniform int       uFirst;    // 1 on the first pass, reading the HDR scene
uniform ivec2     uSrcSize;  // size of uTexture, so out-of-range texels are skipped
uniform int       uHiMode;   // 0 below the highlight scale, 1 form the regions, 2 carry them up
uniform float     uHiPower;  // p: how much small bright regions count (1 = mean, high = near the max)

void fetchAt(ivec2 s, out float mean, out float count, out float hi) {
  vec4 t = texelFetch(uTexture, s, 0);
  if (uFirst != 0) { mean = dot(t.rgb, vec3(0.2126, 0.7152, 0.0722)); count = 1.0; hi = 0.0; }
  else             { mean = t.r; count = t.g; hi = t.b; }
}

bool inside(ivec2 s) { return s.x < uSrcSize.x && s.y < uSrcSize.y; }

void main() {
  ivec2 d = ivec2(gl_FragCoord.xy);
  float p = clamp(uHiPower, 1.0, 8.0);
  float sum = 0.0, n = 0.0, hiSum = 0.0;
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < 2; ++i) {
      ivec2 s = d * 2 + ivec2(i, j);
      if (!inside(s)) continue;
      float m, c, h;
      fetchAt(s, m, c, h);
      sum   += m * c;
      n     += c;
      hiSum += h * c;          // area-weighted mean of region^p, carried up
    }
  }

  float region = 0.0;
  if (uHiMode == 2) {
    region = (n > 0.0) ? hiSum / n : 0.0;
  } else if (uHiMode == 1) {
    float powSum = 0.0, windows = 0.0;
    for (int oy = 0; oy < 2; ++oy) {
      for (int ox = 0; ox < 2; ++ox) {
        float ws = 0.0, wn = 0.0;
        for (int j = 0; j < 2; ++j) {
          for (int i = 0; i < 2; ++i) {
            ivec2 s = d * 2 + ivec2(ox + i, oy + j);
            if (!inside(s)) continue;
            float m, c, h;
            fetchAt(s, m, c, h);
            ws += m * c;
            wn += c;
          }
        }
        if (wn > 0.0) {
          // Clamp before the power: 1e4^8 = 1e32 still fits a float.
          powSum  += pow(min(ws / wn, 1.0e4), p);
          windows += 1.0;
        }
      }
    }
    region = (windows > 0.0) ? powSum / windows : 0.0;
  }
  FragColor = vec4(n > 0.0 ? sum / n : 0.0, n, region, 1.0);
}
