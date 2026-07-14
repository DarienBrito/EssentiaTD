"""Concept A ("Readout E") palette exploration.

Locked geometry (from gen_concepts.concept_a): an E from 5 horizontal bins,
lengths [arm, stub, arm, stub, arm]. Only the PALETTE changes here so the
options are compared on color alone. Outputs one square per palette + a 3x2
contact sheet.

Run:  python assets/v3/gen_palettes.py
"""

import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_PATH = os.path.normpath(os.path.join(HERE, "..", "fonts", "Jura.ttf"))

SS = 3
TRACKING_EM = 0.28
WORD = "EssentiaTD"
LABEL_COL = (138, 138, 142)

# ---- locked concept-A geometry ----
LENGTHS = [1.0, 0.36, 0.78, 0.36, 1.0]   # arm, stub, arm, stub, arm
E_WIDTH = 0.30
THICK   = 0.115      # * E width
GAP     = 0.105      # * E width

# ---- palettes: rows are top->bottom, matching LENGTHS ----
# each: name, bg, [5 row colors], word color, glow (bool)
PALETTES = [
    ("Brand Red", (12, 12, 14),
     [(246, 98, 103), (128, 48, 53), (229, 72, 77), (128, 48, 53), (204, 62, 67)],
     (229, 229, 229), True),

    ("Spectral Heat", (12, 12, 14),
     [(255, 194, 74), (122, 58, 46), (255, 106, 77), (138, 53, 64), (229, 72, 77)],
     (229, 229, 229), True),

    ("Campaign Teal", (12, 12, 14),
     [(120, 230, 220), (28, 95, 90), (51, 214, 201), (28, 95, 90), (42, 157, 149)],
     (229, 229, 229), True),

    ("Teal + Gold", (12, 12, 14),
     [(51, 214, 201), (28, 95, 90), (233, 180, 76), (28, 95, 90), (42, 157, 149)],
     (229, 229, 229), True),

    ("Mono + Red", (12, 12, 14),
     [(242, 242, 242), (74, 74, 78), (229, 72, 77), (74, 74, 78), (200, 200, 205)],
     (229, 229, 229), True),

    ("Light Mode", (244, 244, 242),
     [(26, 26, 30), (201, 201, 201), (229, 72, 77), (201, 201, 201), (26, 26, 30)],
     (42, 42, 46), False),
]


def load_font(size):
    f = ImageFont.truetype(FONT_PATH, size)
    try:
        f.set_variation_by_axes([300])
    except Exception:
        pass
    return f


def draw_e(d, cx, cy, m, colors):
    u = m * E_WIDTH
    t = u * THICK
    g = u * GAP
    h = len(LENGTHS) * t + (len(LENGTHS) - 1) * g
    x0 = cx - u / 2.0
    y = cy - h / 2.0
    for L, c in zip(LENGTHS, colors):
        d.rounded_rectangle([x0, y, x0 + L * u, y + t], radius=t / 2.0, fill=c)
        y += t + g


def draw_wordmark(img, cx, baseline_y, font_size, fill):
    font = load_font(font_size)
    tracking = TRACKING_EM * font_size
    total = sum(font.getlength(ch) for ch in WORD) + tracking * (len(WORD) - 1)
    d = ImageDraw.Draw(img)
    x = cx - total / 2.0
    for ch in WORD:
        d.text((x, baseline_y), ch, font=font, fill=fill, anchor="ls")
        x += font.getlength(ch) + tracking


def render(bg, colors, word, glow, W, H, out=None):
    W3, H3, m = W * SS, H * SS, min(W, H) * SS
    base = Image.new("RGBA", (W3, H3), bg + (255,))
    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    draw_e(ImageDraw.Draw(mark), W3 / 2.0, H3 * 0.41, m, colors)
    if glow:
        gl = mark.filter(ImageFilter.GaussianBlur(radius=m * 0.013))
        a = gl.getchannel("A").point(lambda v: int(v * 0.55))
        gl.putalpha(a)
        base.alpha_composite(gl)
    base.alpha_composite(mark)
    draw_wordmark(base, W3 / 2.0, H3 * 0.705, int(m * 0.047), word)
    final = base.resize((W, H), Image.LANCZOS).convert("RGB")
    if out:
        final.save(out)
        print("wrote", out)
    return final


def slug(name):
    return name.lower().replace(" + ", "-").replace(" ", "-")


if __name__ == "__main__":
    tiles = []
    for name, bg, colors, word, glow in PALETTES:
        p = os.path.join(HERE, f"paletteA-{slug(name)}.png")
        render(bg, colors, word, glow, 1400, 1400, p)
        tiles.append((name, p))

    # 3x2 contact sheet
    cols, rows, tile, gap, margin, labelh = 3, 2, 900, 48, 60, 90
    W = cols * tile + (cols - 1) * gap + 2 * margin
    H = rows * (tile + labelh) + (rows - 1) * gap + 2 * margin
    sheet = Image.new("RGB", (W, H), (20, 20, 23))
    d = ImageDraw.Draw(sheet)
    font = load_font(40)
    for i, (name, path) in enumerate(tiles):
        r, c = divmod(i, cols)
        x = margin + c * (tile + gap)
        y = margin + r * (tile + labelh + gap)
        img = Image.open(path).resize((tile, tile), Image.LANCZOS)
        sheet.paste(img, (x, y))
        d.text((x + tile / 2.0, y + tile + 46), name, font=font,
               fill=LABEL_COL, anchor="mm")
    out = os.path.join(HERE, "palette-sheet.png")
    sheet.save(out)
    print("wrote", out)
