"""Font exploration for the EssentiaTD wordmark.

Shows 6 candidate typefaces against BOTH marks, in the Spectral Heat palette:
  - NEW      : Concept A monogram "E" (spectrum-bin readout)
  - ORIGINAL : the sine waveform with Gaussian amplitude envelope
Wordmarks are width-normalized (binary-searched size) so you compare letterform,
not point size.

Run:  python assets/v3/gen_fonts.py
"""

import os
import math
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.normpath(os.path.join(HERE, "..", "fonts"))
SS = 3
WORD = "EssentiaTD"
BG = (12, 12, 14)

# Spectral Heat palette
HEAT_ROWS = [(255, 194, 74), (122, 58, 46), (255, 106, 77), (138, 53, 64), (229, 72, 77)]
WAVE_DEEP = (146, 52, 57)
WAVE_HOT = (255, 194, 74)
DOT = (229, 72, 77)
WORDCOL = (229, 229, 229)
LABELCOL = (150, 150, 154)
HEADCOL = (120, 120, 124)

# label, file, variable-weight (or None for static), tracking em
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
        w = word_width(f, tracking_em * mid)
        if w > target_w:
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


def draw_e(d, cx, cy, u):
    """Monogram E; u = E width. Rows: arm,stub,arm,stub,arm."""
    lengths = [1.0, 0.36, 0.78, 0.36, 1.0]
    t = u * 0.115
    g = u * 0.105
    h = 5 * t + 4 * g
    x0 = cx - u / 2.0
    y = cy - h / 2.0
    for L, c in zip(lengths, HEAT_ROWS):
        d.rounded_rectangle([x0, y, x0 + L * u, y + t], radius=t / 2.0, fill=c)
        y += t + g


def draw_wave(d, x0, x1, yc, amp, w):
    """Gaussian-enveloped sine, warm ramp by |amplitude|, round caps + end dots."""
    n = 260
    xr = x1 - x0
    mid = (x0 + x1) / 2.0
    sigma = xr / 5.0
    pts = []
    for i in range(n + 1):
        t = i / n
        x = x0 + t * xr
        env = math.exp(-((x - mid) ** 2) / (2 * sigma ** 2))
        s = math.sin(2 * math.pi * 3 * t)
        y = yc - env * amp * s
        pts.append((x, y, min(1.0, env * abs(s))))
    r = w / 2.0
    for i in range(n):
        (x1p, y1p, m1), (x2p, y2p, m2) = pts[i], pts[i + 1]
        c = lerp(WAVE_DEEP, WAVE_HOT, (m1 + m2) / 2.0)
        d.line([(x1p, y1p), (x2p, y2p)], fill=c, width=int(round(w)))
        d.ellipse([x2p - r, y2p - r, x2p + r, y2p + r], fill=c)
    d.ellipse([x0 - r * 1.7, yc - r * 1.7, x0 + r * 1.7, yc + r * 1.7], fill=DOT)
    d.ellipse([x1 - r * 1.7, yc - r * 1.7, x1 + r * 1.7, yc + r * 1.7], fill=DOT)


def cell(kind, file, weight, tracking_em, Wc, Hc):
    W3, H3 = Wc * SS, Hc * SS
    base = Image.new("RGBA", (W3, H3), BG + (255,))
    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    md = ImageDraw.Draw(mark)
    if kind == "new":
        draw_e(md, W3 / 2.0, H3 * 0.34, u=0.40 * H3)
    else:
        draw_wave(md, W3 * 0.15, W3 * 0.85, H3 * 0.33, amp=0.17 * H3, w=0.020 * H3)
    glow = mark.filter(ImageFilter.GaussianBlur(radius=H3 * 0.012))
    a = glow.getchannel("A").point(lambda v: int(v * 0.5))
    glow.putalpha(a)
    base.alpha_composite(glow)
    base.alpha_composite(mark)
    draw_word(base, file, weight, tracking_em, W3 / 2.0, H3 * 0.74, target_w=0.54 * W3)
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
    col1 = margin + gutter
    col2 = col1 + Wc + colgap
    d.text((col1 + Wc / 2, margin + header / 2), "NEW  (monogram E)",
           font=hf, fill=HEADCOL, anchor="mm")
    d.text((col2 + Wc / 2, margin + header / 2), "ORIGINAL  (waveform)",
           font=hf, fill=HEADCOL, anchor="mm")
    y = margin + header
    for label, file, weight, tr in FONTSPECS:
        d.text((margin, y + Hc / 2), label, font=lf, fill=LABELCOL, anchor="lm")
        sheet.paste(cell("new", file, weight, tr, Wc, Hc), (col1, y))
        sheet.paste(cell("orig", file, weight, tr, Wc, Hc), (col2, y))
        y += Hc + rowgap
    out = os.path.join(HERE, "font-compare.png")
    sheet.save(out)
    print("wrote", out, f"({GW}x{GH})")


if __name__ == "__main__":
    build()
