"""EssentiaTD logo v2 — refined render of the original mark.

Same brand DNA as assets/gen_logo.py:
  - sine wave with Gaussian amplitude envelope (sigma = x_range/5, 3 cycles)
  - accent dot at each endpoint
  - red #e5484d on near-black #0c0c0e, Jura Light wordmark, 0.28em tracking

v2 refinements (execution only, not identity):
  - soft neon glow under the crisp stroke (two-radius blur composite)
  - stroke gradient within the red family: deeper red at the tapered tails,
    brighter/warmer red at the central crest (mix driven by the envelope)
  - wordmark drawn glyph-by-glyph with true letter tracking
  - per-aspect-ratio composition (square recomposed, not letterboxed wide)
  - barely-there radial vignette for depth
  - 3x supersampled rendering, LANCZOS downscale

Outputs PNGs directly with Pillow (no SVG->PNG conversion) and hand-written
SVG masters with linearGradient + feGaussianBlur, Jura embedded as base64.

Run:  python assets/v2/gen_logo_v2.py
"""

import base64
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

# ── Brand constants (locked) ──────────────────────────────────────────
NUM_CYCLES = 3
NUM_POINTS = 600
BG_COLOR = "#0c0c0e"
ACCENT = "#e5484d"          # endpoint dots
RED_DEEP = "#a83238"        # tapered tails
RED_BRIGHT = "#ff5f55"      # central crest (brighter + slightly warmer)
TEXT_COLOR = "#e5e5e5"
TEXT_OPACITY = 0.92         # nudged up from 0.85 for presence
TRACKING_EM = 0.28
LABEL = "EssentiaTD"
FONT_WEIGHT = 300           # Jura variable axis; 300 = Light

DIR = os.path.dirname(os.path.abspath(__file__))
FONT_PATH = os.path.join(DIR, "..", "fonts", "Jura.ttf")

SS = 3  # supersampling factor for PNG rendering

# ── Per-aspect composition (fractions of canvas) ──────────────────────
LAYOUTS = {
    "square": dict(
        margin_x=0.14,       # wave side padding
        wave_cy=0.425,       # wave centerline y
        amp=0.135,           # wave amplitude (of h)
        stroke=0.0078,       # stroke width (of h)
        text_cy=0.71,        # wordmark optical center y
        text_w=0.56,         # wordmark target width (of w)
    ),
    "wide": dict(
        margin_x=0.19,
        wave_cy=0.415,
        amp=0.115,
        stroke=0.0083,
        text_cy=0.76,
        text_w=0.33,
    ),
}

# ── Outputs: (basename, width, height, layout key, formats) ──────────
OUTPUTS = [
    ("logo-square", 2048, 2048, "square", ("png", "svg")),
    ("logo-wide",   2560, 1440, "wide",   ("png", "svg")),
]

# Glow tuning (relative to stroke width)
GLOW_TIGHT_R = 1.8
GLOW_TIGHT_A = 0.62
GLOW_WIDE_R = 6.5
GLOW_WIDE_A = 0.28
DOT_SCALE = 1.75            # dot radius = stroke * DOT_SCALE

# Vignette (barely-there)
VIG_CENTER = (17, 17, 20)   # lifted center tone (bg is 12,12,14)
VIG_EXP = 1.8
VIG_RX = 0.70               # ellipse radii as canvas fractions
VIG_RY = 0.70


# ── Helpers ───────────────────────────────────────────────────────────

def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(c1, c2, t):
    return tuple(round(a + (b - a) * t) for a, b in zip(c1, c2))


def rgb_hex(c):
    return "#{:02x}{:02x}{:02x}".format(*c)


def envelope_at(t):
    """Gaussian envelope over normalized t in [0,1]; sigma = range/5."""
    return math.exp(-((t - 0.5) ** 2) / (2 * (1 / 5) ** 2))


def wave_points(x_start, x_end, y_center, amplitude):
    """The brand waveform: enveloped sine, identical math to v1."""
    pts, envs = [], []
    for i in range(NUM_POINTS + 1):
        t = i / NUM_POINTS
        env = envelope_at(t)
        x = x_start + t * (x_end - x_start)
        y = y_center - env * amplitude * math.sin(2 * math.pi * NUM_CYCLES * t)
        pts.append((x, y))
        envs.append(env)
    return pts, envs


def stroke_color(env):
    """Envelope-driven gradient: deep red tails -> bright warm crest."""
    return mix(hex_rgb(RED_DEEP), hex_rgb(RED_BRIGHT), env)


def scaled_alpha(img, factor):
    r, g, b, a = img.split()
    a = a.point(lambda v: round(v * factor))
    return Image.merge("RGBA", (r, g, b, a))


def fit_font_size(target_w, lo=8, hi=600):
    """Binary-search the font size whose tracked label width hits target_w."""
    def width_at(size):
        f = ImageFont.truetype(FONT_PATH, size)
        f.set_variation_by_axes([FONT_WEIGHT])
        scratch = ImageDraw.Draw(Image.new("L", (8, 8)))
        adv = sum(scratch.textlength(c, font=f) for c in LABEL)
        return adv + TRACKING_EM * size * (len(LABEL) - 1)

    while hi - lo > 1:
        mid = (lo + hi) // 2
        if width_at(mid) < target_w:
            lo = mid
        else:
            hi = mid
    return lo


def text_baseline_y(font_size, vh, lay):
    """Baseline y (final-res px) that centers the label glyphs at text_cy."""
    f = ImageFont.truetype(FONT_PATH, font_size)
    f.set_variation_by_axes([FONT_WEIGHT])
    scratch = ImageDraw.Draw(Image.new("L", (8, 8)))
    bbox = scratch.textbbox((0, 0), LABEL, font=f)     # anchor 'la'
    y_draw = lay["text_cy"] * vh - (bbox[1] + bbox[3]) / 2
    return y_draw + f.getmetrics()[0]


def make_vignette_bg(w, h, cx_frac, cy_frac):
    """Solid bg with an almost-imperceptible radial lift behind the mark."""
    small_w, small_h = 320, max(180, round(320 * h / w))
    mask = Image.new("L", (small_w, small_h))
    px = mask.load()
    cx, cy = cx_frac * small_w, cy_frac * small_h
    rx, ry = VIG_RX * small_w, VIG_RY * small_h
    for j in range(small_h):
        for i in range(small_w):
            d = math.sqrt(((i - cx) / rx) ** 2 + ((j - cy) / ry) ** 2)
            px[i, j] = round(255 * max(0.0, 1.0 - d) ** VIG_EXP)
    mask = mask.resize((w, h), Image.BILINEAR)
    dark = Image.new("RGB", (w, h), hex_rgb(BG_COLOR))
    light = Image.new("RGB", (w, h), VIG_CENTER)
    return Image.composite(light, dark, mask).convert("RGBA")


# ── PNG renderer ──────────────────────────────────────────────────────

def render_png(path, vw, vh, lay):
    W, H = vw * SS, vh * SS
    stroke = lay["stroke"] * vh * SS
    dot_r = stroke * DOT_SCALE
    margin = lay["margin_x"] * W
    cy = lay["wave_cy"] * H
    amp = lay["amp"] * H

    dot_l, dot_rt = margin, W - margin
    x0, x1 = dot_l + dot_r, dot_rt - dot_r
    pts, envs = wave_points(x0, x1, cy, amp)

    # -- wave + dots on a transparent layer (crisp, supersampled) --
    wave = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(wave)
    half = stroke / 2
    for i in range(len(pts) - 1):
        env_mid = (envs[i] + envs[i + 1]) / 2
        col = stroke_color(env_mid) + (255,)
        d.line([pts[i], pts[i + 1]], fill=col, width=round(stroke))
        jx, jy = pts[i + 1]
        d.ellipse([jx - half, jy - half, jx + half, jy + half], fill=col)
    # round cap at the start
    sx, sy = pts[0]
    d.ellipse([sx - half, sy - half, sx + half, sy + half],
              fill=stroke_color(envs[0]) + (255,))
    # endpoint dots (kept at the classic accent red)
    acc = hex_rgb(ACCENT) + (255,)
    for cx in (dot_l, dot_rt):
        d.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=acc)

    wave = wave.resize((vw, vh), Image.LANCZOS)

    # -- wordmark layer: per-glyph draw with true tracking --
    font_size = fit_font_size(lay["text_w"] * vw)
    font = ImageFont.truetype(FONT_PATH, font_size * SS)
    font.set_variation_by_axes([FONT_WEIGHT])
    text = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    td = ImageDraw.Draw(text)
    tracking_px = TRACKING_EM * font_size * SS
    advances = [td.textlength(c, font=font) for c in LABEL]
    total_w = sum(advances) + tracking_px * (len(LABEL) - 1)
    bbox = td.textbbox((0, 0), LABEL, font=font)   # anchor 'la'
    y_draw = lay["text_cy"] * H - (bbox[1] + bbox[3]) / 2
    x_cursor = (W - total_w) / 2
    col = hex_rgb(TEXT_COLOR) + (round(255 * TEXT_OPACITY),)
    for ch, adv in zip(LABEL, advances):
        td.text((x_cursor, y_draw), ch, font=font, fill=col)
        x_cursor += adv + tracking_px
    text = text.resize((vw, vh), Image.LANCZOS)

    # -- composite: vignette bg -> wide glow -> tight glow -> crisp -> text --
    fstroke = lay["stroke"] * vh
    img = make_vignette_bg(vw, vh, 0.5, lay["wave_cy"])
    glow_wide = scaled_alpha(
        wave.filter(ImageFilter.GaussianBlur(fstroke * GLOW_WIDE_R)), GLOW_WIDE_A)
    glow_tight = scaled_alpha(
        wave.filter(ImageFilter.GaussianBlur(fstroke * GLOW_TIGHT_R)), GLOW_TIGHT_A)
    img = Image.alpha_composite(img, glow_wide)
    img = Image.alpha_composite(img, glow_tight)
    img = Image.alpha_composite(img, wave)
    img = Image.alpha_composite(img, text)

    img.convert("RGB").save(path, "PNG")
    print(f"Wrote {path} ({vw}x{vh}, font {font_size}px)")
    return font_size


# ── SVG writer (vector master, mirrors the PNG numbers) ───────────────

def write_svg(path, vw, vh, lay, font_size, font_b64):
    stroke = round(lay["stroke"] * vh, 2)
    dot_r = round(stroke * DOT_SCALE, 2)
    margin = lay["margin_x"] * vw
    cy = round(lay["wave_cy"] * vh, 2)
    amp = lay["amp"] * vh

    dot_l, dot_rt = round(margin, 2), round(vw - margin, 2)
    x0, x1 = dot_l + dot_r, dot_rt - dot_r
    pts, _ = wave_points(x0, x1, cy, amp)
    step = max(1, NUM_POINTS // 200)           # decimate for a sane path size
    pts = pts[::step] + ([pts[-1]] if (NUM_POINTS % step) else [])
    path_d = "M " + " L ".join(f"{x:.2f},{y:.2f}" for x, y in pts)

    deep, bright = hex_rgb(RED_DEEP), hex_rgb(RED_BRIGHT)
    q = rgb_hex(mix(deep, bright, envelope_at(0.25)))   # quarter-span tone
    text_y = round(text_baseline_y(font_size, vh, lay), 1)
    # letter-spacing trails the last glyph; nudge x to keep glyphs centered
    text_x = round(vw / 2 + TRACKING_EM * font_size / 2, 1)
    g_tight = round(stroke * GLOW_TIGHT_R, 2)
    g_wide = round(stroke * GLOW_WIDE_R, 2)

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 {vw} {vh}">
  <defs>
    <style>
      @font-face {{
        font-family: 'Jura';
        src: url('data:font/truetype;base64,{font_b64}') format('truetype');
        font-weight: 300;
      }}
    </style>
    <linearGradient id="waveGrad" gradientUnits="userSpaceOnUse"
                    x1="{x0:.2f}" y1="0" x2="{x1:.2f}" y2="0">
      <stop offset="0" stop-color="{RED_DEEP}"/>
      <stop offset="0.25" stop-color="{q}"/>
      <stop offset="0.5" stop-color="{RED_BRIGHT}"/>
      <stop offset="0.75" stop-color="{q}"/>
      <stop offset="1" stop-color="{RED_DEEP}"/>
    </linearGradient>
    <radialGradient id="vig" cx="0.5" cy="{lay['wave_cy']}" r="0.7">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.028"/>
      <stop offset="1" stop-color="#ffffff" stop-opacity="0"/>
    </radialGradient>
    <filter id="glowTight" x="-60%" y="-250%" width="220%" height="600%">
      <feGaussianBlur stdDeviation="{g_tight}"/>
    </filter>
    <filter id="glowWide" x="-60%" y="-250%" width="220%" height="600%">
      <feGaussianBlur stdDeviation="{g_wide}"/>
    </filter>
    <g id="mark">
      <circle cx="{dot_l}" cy="{cy}" r="{dot_r}" fill="{ACCENT}"/>
      <circle cx="{dot_rt}" cy="{cy}" r="{dot_r}" fill="{ACCENT}"/>
      <path d="{path_d}" fill="none" stroke="url(#waveGrad)"
            stroke-width="{stroke}" stroke-linecap="round" stroke-linejoin="round"/>
    </g>
  </defs>
  <rect width="{vw}" height="{vh}" fill="{BG_COLOR}"/>
  <rect width="{vw}" height="{vh}" fill="url(#vig)"/>
  <use xlink:href="#mark" filter="url(#glowWide)" opacity="{GLOW_WIDE_A}"/>
  <use xlink:href="#mark" filter="url(#glowTight)" opacity="{GLOW_TIGHT_A}"/>
  <use xlink:href="#mark"/>
  <text x="{text_x}" y="{text_y}" text-anchor="middle" fill="{TEXT_COLOR}"
        font-family="'Jura', 'Helvetica Neue', sans-serif"
        font-size="{font_size}" font-weight="300" letter-spacing="{TRACKING_EM}em"
        opacity="{TEXT_OPACITY}">{LABEL}</text>
</svg>
'''
    with open(path, "w", encoding="utf-8") as f:
        f.write(svg)
    print(f"Wrote {path} ({vw}x{vh})")


# ── Main ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    with open(FONT_PATH, "rb") as f:
        font_b64 = base64.b64encode(f.read()).decode("ascii")

    for base, w, h, layout, formats in OUTPUTS:
        lay = LAYOUTS[layout]
        font_size = None
        if "png" in formats:
            font_size = render_png(os.path.join(DIR, base + ".png"), w, h, lay)
        if "svg" in formats:
            if font_size is None:
                font_size = fit_font_size(lay["text_w"] * w)
            write_svg(os.path.join(DIR, base + ".svg"), w, h, lay,
                      font_size, font_b64)
