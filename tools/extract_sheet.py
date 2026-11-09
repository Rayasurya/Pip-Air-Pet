"""Cuts the 3x3 Pip expression sheet into native-resolution 1-bit sprites.
The sheet draws each art pixel as ~8x8 screen pixels; this finds the grid and samples it."""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

NAMES = ["happy", "stuffy", "gasping", "hot", "dry", "damp", "asleep", "relieved", "loved"]
CELLS_X = [(100, 470), (570, 940), (1020, 1420)]
CELLS_Y = [(20, 315), (360, 628), (670, 948)]  # frame art only, labels excluded


def sample(lit: np.ndarray, px: float) -> np.ndarray:
    best = None
    for ox in np.arange(0, px, 1):
        for oy in np.arange(0, px, 1):
            xs = np.arange(ox + px / 2, lit.shape[1], px).astype(int)
            ys = np.arange(oy + px / 2, lit.shape[0], px).astype(int)
            g = lit[np.ix_(ys, xs)]
            # score: how uniform each cell is around its sampled centre
            score = 0
            for d in (-2, 2):
                yy = np.clip(ys + d, 0, lit.shape[0] - 1)
                xx = np.clip(xs + d, 0, lit.shape[1] - 1)
                score += (lit[np.ix_(yy, xx)] == g).sum()
            if best is None or score > best[0]:
                best = (score, g)
    return best[1]


def main(sheet: str, out: str) -> None:
    a = np.asarray(Image.open(sheet).convert("RGB")).astype(int)
    lit = a[:, :, 2] > 128
    Path(out).mkdir(parents=True, exist_ok=True)
    for i, name in enumerate(NAMES):
        (x0, x1), (y0, y1) = CELLS_X[i % 3], CELLS_Y[i // 3]
        g = sample(lit[y0:y1, x0:x1], 8.0)
        ys, xs = np.where(g)
        g = g[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        Image.fromarray((g * 255).astype(np.uint8)).save(f"{out}/{name}.png")
        print(name, g.shape[1], "x", g.shape[0])


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
