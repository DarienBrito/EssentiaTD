"""EssentiaTD v3 logo concepts - parameterized generator.

Three re-imagined marks (no waveform):
  A - Monogram: an "E" constructed from horizontal spectrum lines
  B - Radial: 12-tick chroma / pitch-class ring, one key tick lit, essence dot at center
  C - Spectrum bars: five vertical bars (five plugins), tonal red ramp by magnitude

Run:  python assets/v3/gen_concepts.py   (from anywhere; paths resolve from this file)
Outputs concept-{a,b,c}-{square,wide}.png + contact-sheet.png next to this file.
"""

import os
import math
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_PATH = os.path.normpath(os.path.join(HERE, "..", "fonts", "Jura.ttf"))

# ---------------------------------------------------------------- brand fixed
SS = 3                                # supersample factor
BG      = (12, 12, 14, 255)           # #0c0c0e
ACCENT  = (229, 72, 77)               # #e5484d
BRIGHT  = (246, 98, 103)              # hot end of red ramp
MID     = (204, 62, 67)
DEEP    = (146, 52, 57)               # dark end of red ramp
DIM     = (128, 48, 53)               # quiet structural elements
TEXT    = (229, 229, 229)             # #e5e5e5
LABEL   = (138, 138, 142)
TRACKING_EM = 0.28
WORD = "EssentiaTD"

# ------------------------------------------------------------------- helpers
def load_font(size):
    f = ImageFont.truetype(FONT_PATH, size)
    try:
        f.set_variation_by_axes([300])  # Jura Light
    except Exception:
        pass
    return f


def lerp(c1, c2, t):
    return tuple(int(round(c1[i] + (c2[i] - c1[i]) * t)) for i in range(3))


def scale_alpha(img, factor):
    a = img.getchannel("A").point(lambda v: int(v * factor))
    img.putalpha(a)
    return img


def capsule(d, p1, p2, w, color):
    """Thick line with round caps."""
    r = w / 2.0
    d.line([p1, p2], fill=color, width=int(round(w)))
    d.ellipse([p1[0] - r, p1[1] - r, p1[0] + r, p1[1] + r], fill=color)
    d.ellipse([p2[0] - r, p2[1] - r, p2[0] + r, p2[1] + r], fill=color)


def wordmark_width(font, text, tracking_px):
    return sum(font.getlength(ch) for ch in text) + tracking_px * (len(text) - 1)


def draw_wordmark(img, cx, baseline_y, font_size, text=WORD, fill=TEXT):
    """Glyph-by-glyph for true 0.28em tracking (Pillow has no native tracking)."""
    font = load_font(font_size)
    tracking = TRACKING_EM * font_size
    total = wordmark_width(font, text, tracking)
    d = ImageDraw.Draw(img)
    x = cx - total / 2.0
    for ch in text:
        d.text((x, baseline_y), ch, font=font, fill=fill, anchor="ls")
        x += font.getlength(ch) + tracking


# ------------------------------------------------------------------ concepts
# Each concept fn draws the mark (crisp layer) centered at (cx, cy);
# m = min(canvas W, H) at supersampled scale.

def concept_a(d, cx, cy, m, p):
    """Monogram E from horizontal spectrum lines. Arms lit, spine stubs quiet."""
    u = m * p.get("width", 0.30)          # E width
    t = u * p.get("thickness", 0.115)     # line thickness
    g = u * p.get("gap", 0.105)           # gap between lines
    lengths = p.get("lengths", [1.0, 0.36, 0.78, 0.36, 1.0])
    colors  = p.get("colors",  [BRIGHT, DIM, ACCENT, DIM, MID])
    h = len(lengths) * t + (len(lengths) - 1) * g
    x0 = cx - u / 2.0
    y = cy - h / 2.0
    for L, c in zip(lengths, colors):
        d.rounded_rectangle([x0, y, x0 + L * u, y + t], radius=t / 2.0, fill=c)
        y += t + g


def concept_b(d, cx, cy, m, p):
    """Chroma ring: 12 pitch-class ticks, detected key spikes outward,
    essence dot at center. (Accent points OUT so it reads as an analyzer
    peak, not a clock hand.)"""
    R       = m * p.get("radius", 0.175)      # ring outer radius
    tick_l  = m * p.get("tick_len", 0.048)
    tick_w  = m * p.get("tick_w", 0.018)
    acc_ext = m * p.get("accent_ext", 0.055)  # how far the key tick spikes out
    acc_w   = m * p.get("accent_w", 0.021)
    dot_r   = m * p.get("dot_r", 0.021)
    quiet   = p.get("quiet_color", (170, 61, 66))
    cy += acc_ext / 2.0                       # keep optical center despite spike
    for i in range(12):
        ang = math.radians(-90 + i * 30)
        ux, uy = math.cos(ang), math.sin(ang)
        p_in = (cx + ux * (R - tick_l), cy + uy * (R - tick_l))
        if i == 0:
            p_out = (cx + ux * (R + acc_ext), cy + uy * (R + acc_ext))
            capsule(d, p_in, p_out, acc_w, BRIGHT)
        else:
            p_out = (cx + ux * R, cy + uy * R)
            capsule(d, p_in, p_out, tick_w, quiet)
    d.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=ACCENT)


def concept_c(d, cx, cy, m, p):
    """Five spectrum bars (five plugins), brightness follows magnitude."""
    bw    = m * p.get("bar_w", 0.050)
    gap   = m * p.get("gap", 0.036)
    max_h = m * p.get("max_h", 0.30)
    heights = p.get("heights", [0.62, 1.0, 0.74, 0.47, 0.30])
    n = len(heights)
    total_w = n * bw + (n - 1) * gap
    x = cx - total_w / 2.0
    y0 = cy + max_h / 2.0                      # shared baseline
    hmin, hmax = min(heights), max(heights)
    r = bw / 2.0
    for hfrac in heights:
        h = hfrac * max_h
        t = (hfrac - hmin) / (hmax - hmin) if hmax > hmin else 1.0
        c = lerp(DEEP, BRIGHT, t)
        # semicircle cap + rectangle, drawn manually (rounded_rectangle with
        # partial corners leaves hairline seams at float coordinates)
        d.ellipse([x, y0 - h, x + bw, y0 - h + bw], fill=c)
        d.rectangle([x, y0 - h + r, x + bw, y0], fill=c)
        x += bw + gap


CONCEPTS = {
    "a": (concept_a, {}),
    "b": (concept_b, {}),
    "c": (concept_c, {}),
}

# ------------------------------------------------------------------- renders
def render(concept_key, W, H, out_path,
           mark_cy=0.41, word_baseline=0.705, word_size_frac=0.047,
           glow_radius_frac=0.013, glow_alpha=0.55):
    fn, params = CONCEPTS[concept_key]
    W3, H3, m = W * SS, H * SS, min(W, H) * SS
    base = Image.new("RGBA", (W3, H3), BG)

    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    fn(ImageDraw.Draw(mark), W3 / 2.0, H3 * mark_cy, m, params)

    glow = mark.filter(ImageFilter.GaussianBlur(radius=m * glow_radius_frac))
    base.alpha_composite(scale_alpha(glow, glow_alpha))
    base.alpha_composite(mark)

    draw_wordmark(base, W3 / 2.0, H3 * word_baseline, int(m * word_size_frac))

    final = base.resize((W, H), Image.LANCZOS).convert("RGB")
    final.save(out_path)
    print("wrote", out_path)


def contact_sheet(out_path, tile=1120, gap=60, margin=60, label_size=52):
    keys = ["a", "b", "c"]
    W = 3 * tile + 2 * gap + 2 * margin
    H = margin + tile + 120
    sheet = Image.new("RGB", (W, H), BG[:3])
    d = ImageDraw.Draw(sheet)
    font = load_font(label_size)
    for i, k in enumerate(keys):
        img = Image.open(os.path.join(HERE, f"concept-{k}-square.png"))
        img = img.resize((tile, tile), Image.LANCZOS)
        x = margin + i * (tile + gap)
        sheet.paste(img, (x, margin))
        d.text((x + tile / 2.0, margin + tile + 58), k.upper(),
               font=font, fill=LABEL, anchor="mm")
    sheet.save(out_path)
    print("wrote", out_path)


if __name__ == "__main__":
    for k in ["a", "b", "c"]:
        render(k, 2048, 2048, os.path.join(HERE, f"concept-{k}-square.png"),
               mark_cy=0.41, word_baseline=0.705, word_size_frac=0.047)
        render(k, 2560, 1440, os.path.join(HERE, f"concept-{k}-wide.png"),
               mark_cy=0.42, word_baseline=0.76, word_size_frac=0.055)
    contact_sheet(os.path.join(HERE, "contact-sheet.png"))
