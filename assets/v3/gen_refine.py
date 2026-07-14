"""Refinement options for the ORIGINAL waveform logo, in red.

Five refined directions of the same Gaussian-enveloped sine mark; only the mark
treatment varies (stroke, endpoint style, gradient, glow, envelope). Wordmark
held in Jura (current) so the comparison is about the mark. Outputs one square
lockup per variant + a contact sheet.

Run:  python assets/v3/gen_refine.py
"""
import os
import math
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.normpath(os.path.join(HERE, "..", "fonts"))
SS = 3
WORD = "EssentiaTD"
BG = (12, 12, 14)
DEEP = (150, 50, 55)          # red ramp, tails
BRIGHT = (248, 104, 108)      # red ramp, crest
DOTC = (229, 72, 77)
WORDCOL = (229, 229, 229)
LABELCOL = (150, 150, 154)

# name, params
VARIANTS = [
    ("Refined Classic", dict(cycles=3, amp=0.16, sigma=5.0, stroke=0.021,
                             dot="filled", dot_r=1.9, glow=0.013, glow_a=0.55)),
    ("Thin & Elegant",  dict(cycles=3, amp=0.145, sigma=5.0, stroke=0.013,
                             dot="filled", dot_r=1.7, glow=0.010, glow_a=0.45)),
    ("Bold Signal",     dict(cycles=3, amp=0.175, sigma=5.0, stroke=0.030,
                             dot="filled", dot_r=1.7, glow=0.018, glow_a=0.7)),
    ("Open Nodes",      dict(cycles=3, amp=0.16, sigma=5.0, stroke=0.020,
                             dot="ring", dot_r=2.4, glow=0.013, glow_a=0.55)),
    ("Calm Swell",      dict(cycles=2, amp=0.17, sigma=4.0, stroke=0.021,
                             dot="filled", dot_r=1.9, glow=0.013, glow_a=0.55)),
]


def load_jura(size):
    f = ImageFont.truetype(os.path.join(FONTS, "Jura.ttf"), size)
    try:
        f.set_variation_by_axes([300])
    except Exception:
        pass
    return f


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def draw_word(img, cx, baseline, target_w):
    tr_em = 0.28
    lo, hi = 8.0, 600.0
    for _ in range(32):
        mid = (lo + hi) / 2
        f = load_jura(int(mid))
        w = sum(f.getlength(c) for c in WORD) + tr_em * mid * (len(WORD) - 1)
        if w > target_w:
            hi = mid
        else:
            lo = mid
    size = int((lo + hi) / 2)
    f = load_jura(size)
    tr = tr_em * size
    total = sum(f.getlength(c) for c in WORD) + tr * (len(WORD) - 1)
    d = ImageDraw.Draw(img)
    x = cx - total / 2.0
    for c in WORD:
        d.text((x, baseline), c, font=f, fill=WORDCOL, anchor="ls")
        x += f.getlength(c) + tr


def draw_wave(d, x0, x1, yc, amp, w, cycles, sigma_div, dot, dot_r_mult):
    n = 300
    xr = x1 - x0
    mid = (x0 + x1) / 2.0
    sigma = xr / sigma_div
    pts = []
    for i in range(n + 1):
        t = i / n
        x = x0 + t * xr
        env = math.exp(-((x - mid) ** 2) / (2 * sigma ** 2))
        s = math.sin(2 * math.pi * cycles * t)
        pts.append((x, yc - env * amp * s, min(1.0, env * abs(s))))
    r = w / 2.0
    for i in range(n):
        (x1p, y1p, m1), (x2p, y2p, m2) = pts[i], pts[i + 1]
        c = lerp(DEEP, BRIGHT, (m1 + m2) / 2.0)
        d.line([(x1p, y1p), (x2p, y2p)], fill=c, width=int(round(w)))
        d.ellipse([x2p - r, y2p - r, x2p + r, y2p + r], fill=c)
    dr = r * dot_r_mult
    for xe in (x0, x1):
        if dot == "filled":
            d.ellipse([xe - dr, yc - dr, xe + dr, yc + dr], fill=DOTC)
        elif dot == "ring":
            rw = max(2, int(w * 0.7))
            d.ellipse([xe - dr, yc - dr, xe + dr, yc + dr], outline=DOTC, width=rw)


def render(p, Wc, Hc, out=None):
    W3, H3 = Wc * SS, Hc * SS
    base = Image.new("RGBA", (W3, H3), BG + (255,))
    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    draw_wave(ImageDraw.Draw(mark), W3 * 0.15, W3 * 0.85, H3 * 0.40,
              amp=p["amp"] * H3, w=p["stroke"] * H3, cycles=p["cycles"],
              sigma_div=p["sigma"], dot=p["dot"], dot_r_mult=p["dot_r"])
    glow = mark.filter(ImageFilter.GaussianBlur(radius=H3 * p["glow"]))
    a = glow.getchannel("A").point(lambda v: int(v * p["glow_a"]))
    glow.putalpha(a)
    base.alpha_composite(glow)
    base.alpha_composite(mark)
    draw_word(base, W3 / 2.0, H3 * 0.70, 0.50 * W3)
    final = base.resize((Wc, Hc), Image.LANCZOS).convert("RGB")
    if out:
        final.save(out)
    return final


def slug(n):
    return n.lower().replace(" & ", "-").replace(" ", "-")


if __name__ == "__main__":
    tiles = []
    for name, p in VARIANTS:
        out = os.path.join(HERE, f"refine-{slug(name)}.png")
        render(p, 1400, 1400, out)
        tiles.append((name, out))
        print("wrote", out)

    cols, rows, tile, gap, margin, labelh = 3, 2, 860, 44, 60, 84
    W = cols * tile + (cols - 1) * gap + 2 * margin
    H = rows * (tile + labelh) + (rows - 1) * gap + 2 * margin
    sheet = Image.new("RGB", (W, H), (18, 18, 21))
    d = ImageDraw.Draw(sheet)
    font = load_jura(38)
    for i, (name, path) in enumerate(tiles):
        r, c = divmod(i, cols)
        x = margin + c * (tile + gap)
        y = margin + r * (tile + labelh + gap)
        sheet.paste(Image.open(path).resize((tile, tile), Image.LANCZOS), (x, y))
        d.text((x + tile / 2.0, y + tile + 42), name, font=font,
               fill=LABELCOL, anchor="mm")
    out = os.path.join(HERE, "refine-sheet.png")
    sheet.save(out)
    print("wrote", out)
