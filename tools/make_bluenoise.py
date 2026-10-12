# Generates shaderpack/textures/bluenoise.raw: 128x128 void-and-cluster blue noise (Ulichney).
# R = the ranking map, G and B = the same map shifted by half / quarter tiles (decorrelated
# extra channels for 2D sample offsets). Format as tools/png_to_raw.py.
import os, struct
import numpy as np

N = 128
SIGMA = 1.9
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, '..', 'shaderpack', 'textures', 'bluenoise.raw')

d = np.minimum(np.arange(N), N - np.arange(N))
kernel = np.exp(-(d[:, None] ** 2 + d[None, :] ** 2) / (2 * SIGMA ** 2))   # wrapped Gaussian


def splat(energy, y, x, sign):
    energy += sign * np.roll(np.roll(kernel, y, 0), x, 1)


rng = np.random.default_rng(1)
pattern = np.zeros((N, N), bool)
energy = np.zeros((N, N))
for i in rng.choice(N * N, N * N // 10, replace=False):
    y, x = divmod(int(i), N)
    pattern[y, x] = True
    splat(energy, y, x, 1)

# Relax the initial pattern: move the tightest cluster point into the largest void.
while True:
    y, x = np.unravel_index(np.argmax(np.where(pattern, energy, -np.inf)), energy.shape)
    pattern[y, x] = False; splat(energy, y, x, -1)
    vy, vx = np.unravel_index(np.argmin(np.where(pattern, np.inf, energy)), energy.shape)
    if (vy, vx) == (y, x):
        pattern[y, x] = True; splat(energy, y, x, 1)
        break
    pattern[vy, vx] = True; splat(energy, vy, vx, 1)

rank = np.zeros((N, N), int)
ones = int(pattern.sum())
# Phase 1: remove cluster points from the initial pattern, ranking downwards.
p = pattern.copy(); e = energy.copy()
for r in range(ones - 1, -1, -1):
    y, x = np.unravel_index(np.argmax(np.where(p, e, -np.inf)), e.shape)
    p[y, x] = False; splat(e, y, x, -1); rank[y, x] = r
# Phase 2: fill voids from the initial pattern, ranking upwards.
p = pattern.copy(); e = energy.copy()
for r in range(ones, N * N):
    y, x = np.unravel_index(np.argmin(np.where(p, np.inf, e)), e.shape)
    p[y, x] = True; splat(e, y, x, 1); rank[y, x] = r

r8 = (rank * 256 // (N * N)).astype(np.uint8)
rgba = np.stack([r8, np.roll(r8, (N // 2, N // 2), (0, 1)), np.roll(r8, (N // 4, 3 * N // 4), (0, 1)),
                 np.full_like(r8, 255)], -1)
with open(out, 'wb') as f:
    f.write(b'HYTX' + struct.pack('<II', N, N) + rgba.tobytes())
print(out)
