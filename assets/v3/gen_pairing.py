"""Refined Classic waveform (red) paired with the front-runner fonts.

Same mark everywhere; only the wordmark typeface changes. Pick the final font.
Run:  python assets/v3/gen_pairing.py
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_refine import draw_wave, BG, WORD, WORDCOL, LABELCOL  # noqa: E402

FONTS = os.path.normpath(os.path.join(HERE, "..", "fonts"))
SS = 3

# Refined Classic params
RC = dict(cycles=3, amp=0.16, sigma=5.0, stroke=0.021,
          dot="filled", dot_r=1.9, glow=0.013, glow_a=0.55)

# label, file, variable-weight (or None), tracking em
FONTSPECS = [
    ("Jura Light  (current)", "Jura.ttf", 300, 0.28),
    ("Space Grotesk", "SpaceGrotesk.ttf", 300, 0.16),
    ("Sora", "Sora.ttf", 300, 0.14),
    ("Chakra Petch", "ChakraPetch-Light.ttf", None, 0.12),
]


def load(file, size, weight):
    f = ImageFont.truetype(os.path.join(FONTS, file), size)
    if weight is not None:
        try:
            f.set_variation_by_axes([weight])
        except Exception:
            pass
    return f


def draw_word(img, file, weight, tr_em, cx, baseline, target_w):
    lo, hi = 8.0, 600.0
    for _ in range(32):
        mid = (lo + hi) / 2
        f = load(file, int(mid), weight)
        w = sum(f.getlength(c) for c in WORD) + tr_em * mid * (len(WORD) - 1)
        hi, lo = (mid, lo) if w > target_w else (hi, mid)
    size = int((lo + hi) / 2)
    f = load(file, size, weight)
    tr = tr_em * size
    total = sum(f.getlength(c) for c in WORD) + tr * (len(WORD) - 1)
    d = ImageDraw.Draw(img)
    x = cx - total / 2.0
    for c in WORD:
        d.text((x, baseline), c, font=f, fill=WORDCOL, anchor="ls")
        x += f.getlength(c) + tr


def render(file, weight, tr_em, Wc, Hc, out=None):
    W3, H3 = Wc * SS, Hc * SS
    base = Image.new("RGBA", (W3, H3), BG + (255,))
    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    draw_wave(ImageDraw.Draw(mark), W3 * 0.15, W3 * 0.85, H3 * 0.40,
              amp=RC["amp"] * H3, w=RC["stroke"] * H3, cycles=RC["cycles"],
              sigma_div=RC["sigma"], dot=RC["dot"], dot_r_mult=RC["dot_r"])
    glow = mark.filter(ImageFilter.GaussianBlur(radius=H3 * RC["glow"]))
    a = glow.getchannel("A").point(lambda v: int(v * RC["glow_a"]))
    glow.putalpha(a)
    base.alpha_composite(glow)
    base.alpha_composite(mark)
    draw_word(base, file, weight, tr_em, W3 / 2.0, H3 * 0.70, 0.50 * W3)
    final = base.resize((Wc, Hc), Image.LANCZOS).convert("RGB")
    if out:
        final.save(out)
    return final


if __name__ == "__main__":
    tiles = []
    for label, file, weight, tr in FONTSPECS:
        slug = label.split()[0].lower()
        out = os.path.join(HERE, f"pair-{slug}.png")
        render(file, weight, tr, 1400, 1400, out)
        tiles.append((label, out))
        print("wrote", out)

    cols, rows, tile, gap, margin, labelh = 2, 2, 900, 44, 60, 84
    W = cols * tile + (cols - 1) * gap + 2 * margin
    H = rows * (tile + labelh) + (rows - 1) * gap + 2 * margin
    sheet = Image.new("RGB", (W, H), (18, 18, 21))
    d = ImageDraw.Draw(sheet)
    font = load("Sora.ttf", 38, 400)
    for i, (label, path) in enumerate(tiles):
        r, c = divmod(i, cols)
        x = margin + c * (tile + gap)
        y = margin + r * (tile + labelh + gap)
        sheet.paste(Image.open(path).resize((tile, tile), Image.LANCZOS), (x, y))
        d.text((x + tile / 2.0, y + tile + 42), label, font=font,
               fill=LABELCOL, anchor="mm")
    sheet.save(os.path.join(HERE, "pairing-sheet.png"))
    print("wrote pairing-sheet.png")
