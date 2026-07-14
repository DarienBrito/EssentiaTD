"""Original waveform logo: Just Red vs Spectral Heat, across 6 fonts.

Columns = color treatment (Just Red | Spectral Heat), rows = typeface.
Reuses the Gaussian-envelope sine mark; only the hot end of the amplitude
ramp changes between columns (red vs amber). Wordmarks width-normalized.

Run:  python assets/v3/gen_compare_orig.py
"""

import os
import math
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.normpath(os.path.join(HERE, "..", "fonts"))
SS = 3
WORD = "EssentiaTD"
BG = (12, 12, 14)

WAVE_DEEP = (146, 52, 57)
DOT = (229, 72, 77)
WORDCOL = (229, 229, 229)
LABELCOL = (150, 150, 154)
HEADCOL = (120, 120, 124)

# column label, hot color at wave crest
COLUMNS = [
    ("JUST RED", (246, 98, 103)),
    ("SPECTRAL HEAT", (255, 194, 74)),
]

# label, file, variable-weight (or None), tracking em
FONTSPECS = [
    ("Jura Light  (current)", "Jura.ttf", 300, 0.28),
    ("Space Grotesk", "SpaceGrotesk.ttf", 300, 0.16),
    ("Chakra Petch", "ChakraPetch-Light.ttf", None, 0.12),
    ("Rajdhani", "Rajdhani-Light.ttf", None, 0.16),
    ("Sora", "Sora.ttf", 300, 0.14),
    ("Space Mono", "SpaceMono-Regular.ttf", None, 0.08),
]


def load(file, size, weight):
    f = ImageFont.truetype(os.path.join(FONTS, file), size)
    if weight is not None:
        try:
            f.set_variation_by_axes([weight])
        except Exception:
            pass
    return f


def word_width(font, tracking_px):
    return sum(font.getlength(c) for c in WORD) + tracking_px * (len(WORD) - 1)


def solve_size(file, weight, tracking_em, target_w):
    lo, hi = 8.0, 600.0
    for _ in range(34):
        mid = (lo + hi) / 2
        f = load(file, int(mid), weight)
        if word_width(f, tracking_em * mid) > target_w:
            hi = mid
        else:
            lo = mid
    return int((lo + hi) / 2)


def draw_word(img, file, weight, tracking_em, cx, baseline, target_w):
    size = solve_size(file, weight, tracking_em, target_w)
    f = load(file, size, weight)
    tr = tracking_em * size
    total = word_width(f, tr)
    d = ImageDraw.Draw(img)
    x = cx - total / 2.0
    for c in WORD:
        d.text((x, baseline), c, font=f, fill=WORDCOL, anchor="ls")
        x += f.getlength(c) + tr


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def draw_wave(d, x0, x1, yc, amp, w, hot):
    n = 260
    xr, mid, sigma = x1 - x0, (x0 + x1) / 2.0, (x1 - x0) / 5.0
    pts = []
    for i in range(n + 1):
        t = i / n
        x = x0 + t * xr
        env = math.exp(-((x - mid) ** 2) / (2 * sigma ** 2))
        s = math.sin(2 * math.pi * 3 * t)
        pts.append((x, yc - env * amp * s, min(1.0, env * abs(s))))
    r = w / 2.0
    for i in range(n):
        (x1p, y1p, m1), (x2p, y2p, m2) = pts[i], pts[i + 1]
        c = lerp(WAVE_DEEP, hot, (m1 + m2) / 2.0)
        d.line([(x1p, y1p), (x2p, y2p)], fill=c, width=int(round(w)))
        d.ellipse([x2p - r, y2p - r, x2p + r, y2p + r], fill=c)
    for xe in (x0, x1):
        d.ellipse([xe - r * 1.7, yc - r * 1.7, xe + r * 1.7, yc + r * 1.7], fill=DOT)


def cell(hot, file, weight, tracking_em, Wc, Hc):
    W3, H3 = Wc * SS, Hc * SS
    base = Image.new("RGBA", (W3, H3), BG + (255,))
    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    draw_wave(ImageDraw.Draw(mark), W3 * 0.15, W3 * 0.85, H3 * 0.33,
              amp=0.17 * H3, w=0.020 * H3, hot=hot)
    glow = mark.filter(ImageFilter.GaussianBlur(radius=H3 * 0.012))
    a = glow.getchannel("A").point(lambda v: int(v * 0.5))
    glow.putalpha(a)
    base.alpha_composite(glow)
    base.alpha_composite(mark)
    draw_word(base, file, weight, tracking_em, W3 / 2.0, H3 * 0.74, 0.54 * W3)
    return base.resize((Wc, Hc), Image.LANCZOS).convert("RGB")


def build():
    Wc, Hc = 940, 600
    gutter, colgap, rowgap, margin, header = 330, 46, 34, 60, 96
    GW = margin + gutter + Wc + colgap + Wc + margin
    GH = margin + header + len(FONTSPECS) * Hc + (len(FONTSPECS) - 1) * rowgap + margin
    sheet = Image.new("RGB", (GW, GH), (18, 18, 21))
    d = ImageDraw.Draw(sheet)
    hf = load("Sora.ttf", 34, 400)
    lf = load("Sora.ttf", 30, 400)
    xs = [margin + gutter, margin + gutter + Wc + colgap]
    for (name, _), x in zip(COLUMNS, xs):
        d.text((x + Wc / 2, margin + header / 2), name, font=hf, fill=HEADCOL, anchor="mm")
    y = margin + header
    for label, file, weight, tr in FONTSPECS:
        d.text((margin, y + Hc / 2), label, font=lf, fill=LABELCOL, anchor="lm")
        for (_, hot), x in zip(COLUMNS, xs):
            sheet.paste(cell(hot, file, weight, tr, Wc, Hc), (x, y))
        y += Hc + rowgap
    out = os.path.join(HERE, "compare-original-red-vs-heat.png")
    sheet.save(out)
    print("wrote", out, f"({GW}x{GH})")


if __name__ == "__main__":
    build()
