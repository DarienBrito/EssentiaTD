"""EssentiaTD final brand identity generator.

Mark  : Refined Classic waveform (Gaussian-enveloped sine, deep->bright red
        amplitude gradient, soft glow, filled endpoint dots).
Word  : "EssentiaTD" in Space Grotesk (weight 300, 0.16em tracking).
Palette: red #e5484d family on #0c0c0e (+ a light-background variant).

Outputs PNGs (Pillow) and SVG masters (wordmark converted to true vector paths
via fontTools, so the SVGs need no font installed).

Run:  python assets/final/gen_final.py
"""
import os
import math
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools.pens.svgPathPen import SVGPathPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.normpath(os.path.join(HERE, "..", "fonts"))
SG = os.path.join(FONTS, "SpaceGrotesk.ttf")
SS = 3
WORD = "EssentiaTD"
TRACK_EM = 0.16

# palette
BG = (12, 12, 14)
DEEP, BRIGHT = (150, 50, 55), (248, 104, 108)     # red ramp: tails -> crest
DOTC = (229, 72, 77)
WORDCOL = (229, 229, 229)
# light variant
LBG = (244, 244, 242)
LDEEP, LBRIGHT = (198, 54, 60), (232, 86, 90)
LDOT = (205, 52, 58)
LWORD = (38, 38, 44)

CYCLES, SIGMA_DIV, DOT_MULT = 3, 5.0, 1.9


def hexc(c):
    return "#%02x%02x%02x" % c


# ----------------------------------------------------------------- PNG side
def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def load_sg(size):
    f = ImageFont.truetype(SG, size)
    try:
        f.set_variation_by_axes([300])
    except Exception:
        pass
    return f


def wave_pts(x0, x1, yc, amp, n=340):
    xr, mid, sigma = x1 - x0, (x0 + x1) / 2.0, (x1 - x0) / SIGMA_DIV
    pts = []
    for i in range(n + 1):
        t = i / n
        x = x0 + t * xr
        env = math.exp(-((x - mid) ** 2) / (2 * sigma ** 2))
        s = math.sin(2 * math.pi * CYCLES * t)
        pts.append((x, yc - env * amp * s, min(1.0, env * abs(s))))
    return pts


def draw_wave(d, x0, x1, yc, amp, w, ramp, dotc):
    pts = wave_pts(x0, x1, yc, amp)
    r = w / 2.0
    for i in range(len(pts) - 1):
        (ax, ay, m1), (bx, by, m2) = pts[i], pts[i + 1]
        c = lerp(ramp[0], ramp[1], (m1 + m2) / 2.0)
        d.line([(ax, ay), (bx, by)], fill=c, width=int(round(w)))
        d.ellipse([bx - r, by - r, bx + r, by + r], fill=c)
    dr = r * DOT_MULT
    for xe in (x0, x1):
        d.ellipse([xe - dr, yc - dr, xe + dr, yc + dr], fill=dotc)


def draw_word_png(img, cx, baseline, target_w, fill):
    lo, hi = 8.0, 900.0
    for _ in range(34):
        mid = (lo + hi) / 2
        f = load_sg(int(mid))
        w = sum(f.getlength(c) for c in WORD) + TRACK_EM * mid * (len(WORD) - 1)
        hi, lo = (mid, lo) if w > target_w else (hi, mid)
    size = int((lo + hi) / 2)
    f = load_sg(size)
    tr = TRACK_EM * size
    total = sum(f.getlength(c) for c in WORD) + tr * (len(WORD) - 1)
    d = ImageDraw.Draw(img)
    x = cx - total / 2.0
    for c in WORD:
        d.text((x, baseline), c, font=f, fill=fill, anchor="ls")
        x += f.getlength(c) + tr


def render_png(W, H, lay, *, word=True, bg=BG, ramp=(DEEP, BRIGHT), dotc=DOTC,
               wordcol=WORDCOL, glow=True, transparent=False, out=None):
    W3, H3 = W * SS, H * SS
    base = Image.new("RGBA", (W3, H3), (0, 0, 0, 0) if transparent else bg + (255,))
    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    md = ImageDraw.Draw(mark)
    D = min(W3, H3)
    draw_wave(md, W3 * lay["x0"], W3 * lay["x1"], H3 * lay["cy"],
              amp=lay["amp"] * D, w=lay["stroke"] * D, ramp=ramp, dotc=dotc)
    if glow:
        g = mark.filter(ImageFilter.GaussianBlur(radius=D * 0.013))
        a = g.getchannel("A").point(lambda v: int(v * 0.55))
        g.putalpha(a)
        base.alpha_composite(g)
    base.alpha_composite(mark)
    if word:
        draw_word_png(base, W3 / 2.0, H3 * lay["wbase"], lay["wtarget"] * W3, wordcol)
    final = base.resize((W, H), Image.LANCZOS)
    if not transparent:
        final = final.convert("RGB")
    if out:
        final.save(out)
    return final


# ----------------------------------------------------------------- SVG side
def wave_svg_path(x0, x1, yc, amp):
    pts = wave_pts(x0, x1, yc, amp)
    return "M " + " L ".join(f"{x:.2f},{y:.2f}" for x, y, _ in pts)


def wordmark_svg(cx, baseline, font_px, fill):
    font = TTFont(SG)
    if "fvar" in font:
        instancer.instantiateVariableFont(font, {"wght": 300}, inplace=True)
    upm = font["head"].unitsPerEm
    cmap = font.getBestCmap()
    gs = font.getGlyphSet()
    hmtx = font["hmtx"]
    s = font_px / upm
    tr = TRACK_EM * font_px
    glyphs = [(cmap[ord(c)]) for c in WORD]
    total = sum(hmtx[g][0] * s for g in glyphs) + tr * (len(glyphs) - 1)
    x = cx - total / 2.0
    parts = []
    for g in glyphs:
        pen = SVGPathPen(gs)
        gs[g].draw(pen)
        d = pen.getCommands()
        if d:
            parts.append(f'<path d="{d}" transform="translate({x:.2f},{baseline:.2f}) '
                         f'scale({s:.5f},{-s:.5f})" fill="{fill}"/>')
        x += hmtx[g][0] * s + tr
    return "\n  ".join(parts)


def build_svg(W, H, lay, *, word=True, bg=BG, ramp=(DEEP, BRIGHT), dotc=DOTC,
              wordcol=WORDCOL, glow=True, out=None):
    D = min(W, H)
    x0, x1, yc = W * lay["x0"], W * lay["x1"], H * lay["cy"]
    amp, w = lay["amp"] * D, lay["stroke"] * D
    path = wave_svg_path(x0, x1, yc, amp)
    dr = (w / 2.0) * DOT_MULT
    grad = (f'<linearGradient id="wg" gradientUnits="userSpaceOnUse" '
            f'x1="{x0:.1f}" y1="0" x2="{x1:.1f}" y2="0">'
            f'<stop offset="0" stop-color="{hexc(ramp[0])}"/>'
            f'<stop offset="0.5" stop-color="{hexc(ramp[1])}"/>'
            f'<stop offset="1" stop-color="{hexc(ramp[0])}"/></linearGradient>')
    glow_svg = ""
    if glow:
        grad += (f'<filter id="gl" x="-30%" y="-60%" width="160%" height="220%">'
                 f'<feGaussianBlur stdDeviation="{D*0.012:.1f}"/></filter>')
        glow_svg = (f'<path d="{path}" fill="none" stroke="url(#wg)" '
                    f'stroke-width="{w:.1f}" stroke-linecap="round" '
                    f'stroke-linejoin="round" filter="url(#gl)" opacity="0.55"/>')
    dots = "".join(f'<circle cx="{xe:.1f}" cy="{yc:.1f}" r="{dr:.1f}" '
                   f'fill="{hexc(dotc)}"/>' for xe in (x0, x1))
    word_svg = ""
    if word:
        fs = lay.get("svg_word_px", 0.052) * W
        word_svg = wordmark_svg(W / 2.0, H * lay["wbase"], fs, hexc(wordcol))
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}">
  <defs>{grad}</defs>
  <rect width="{W}" height="{H}" fill="{hexc(bg)}"/>
  {glow_svg}
  <path d="{path}" fill="none" stroke="url(#wg)" stroke-width="{w:.1f}"
        stroke-linecap="round" stroke-linejoin="round"/>
  {dots}
  {word_svg}
</svg>
'''
    if out:
        with open(out, "w", encoding="utf-8") as f:
            f.write(svg)
    return svg


# ----------------------------------------------------------------- layouts
LOCK = dict(cy=0.40, x0=0.16, x1=0.84, amp=0.155, stroke=0.020,
            wbase=0.705, wtarget=0.50, svg_word_px=0.052)
WIDE = dict(cy=0.41, x0=0.31, x1=0.69, amp=0.175, stroke=0.019,
            wbase=0.72, wtarget=0.30, svg_word_px=0.032)
ICON = dict(cy=0.50, x0=0.13, x1=0.87, amp=0.195, stroke=0.023,
            wbase=0, wtarget=0)


def p(name):
    return os.path.join(HERE, name)


if __name__ == "__main__":
    # PNG lockups
    render_png(2048, 2048, LOCK, out=p("essentiatd-logo-square.png"))
    render_png(2560, 1440, WIDE, out=p("essentiatd-logo-wide.png"))
    render_png(1080, 1080, LOCK, out=p("essentiatd-ig-1080.png"))
    render_png(2048, 2048, LOCK, bg=LBG, ramp=(LDEEP, LBRIGHT), dotc=LDOT,
               wordcol=LWORD, glow=False, out=p("essentiatd-logo-square-light.png"))
    # icons
    render_png(2048, 2048, ICON, word=False, out=p("essentiatd-icon.png"))
    render_png(2048, 2048, ICON, word=False, transparent=True,
               out=p("essentiatd-icon-transparent.png"))
    ic = render_png(2048, 2048, ICON, word=False)
    for sz in (512, 256, 128):
        ic.resize((sz, sz), Image.LANCZOS).save(p(f"essentiatd-icon-{sz}.png"))
    # SVG masters
    build_svg(2048, 2048, LOCK, out=p("essentiatd-logo-square.svg"))
    build_svg(2560, 1440, WIDE, out=p("essentiatd-logo-wide.svg"))
    build_svg(2048, 2048, ICON, word=False, out=p("essentiatd-icon.svg"))
    print("done ->", HERE)
    for f in sorted(os.listdir(HERE)):
        if f.startswith("essentiatd-"):
            print("  ", f)
