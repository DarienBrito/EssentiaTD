"""Generate EssentiaTD logo SVGs and JPGs with a mathematically correct waveform."""

import base64
import math
import os

# ── Shared parameters ──
NUM_CYCLES = 3
MAX_AMPLITUDE = 70
NUM_POINTS = 400
DOT_RADIUS = 6
STROKE_WIDTH = 3.5
MARGIN = 0.15          # fraction of canvas size for padding
BG_COLOR = "#0c0c0e"
ACCENT = "#e5484d"
TEXT_COLOR = "#e5e5e5"

# Jura Light — thin geometric sans, embedded for portability
FONT_PATH = os.path.join(
    os.path.expanduser("~"),
    ".claude", "skills", "canvas-design", "canvas-fonts", "Jura-Light.ttf",
)

# ═══════════════════════════════════════════════════════════════════════
# Output specs — edit this list to generate any resolution / format.
#
# Each entry: (filename, width, height, show_text)
#   - filename extension determines format: .svg or .jpg
#   - show_text: True to include the "EssentiaTD" wordmark below the wave
# ═══════════════════════════════════════════════════════════════════════

OUTPUTS = [
    # SVGs
    ("icon.svg",    512,  512,  False),
    ("logo.svg",    512,  512,  True),
    ("banner.svg",  1920, 1080, True),
    # JPGs
    ("icon.jpg",    512,  512,  False),
    ("logo.jpg",    512,  512,  True),
    ("banner.jpg",  1920, 1080, True),
]


def load_font_base64():
    """Read the font file and return a base64-encoded data URI."""
    with open(FONT_PATH, "rb") as f:
        return base64.b64encode(f.read()).decode("ascii")


def sine_waveform(x_start, x_end, y_center):
    """Generate a sine wave with a Gaussian amplitude envelope."""
    points = []
    x_range = x_end - x_start
    mid = (x_start + x_end) / 2
    sigma = x_range / 5

    for i in range(NUM_POINTS + 1):
        t = i / NUM_POINTS
        x = x_start + t * x_range
        envelope = math.exp(-((x - mid) ** 2) / (2 * sigma ** 2))
        y = y_center - envelope * MAX_AMPLITUDE * math.sin(2 * math.pi * NUM_CYCLES * t)
        points.append((round(x, 2), round(y, 2)))

    return points


def points_to_path(points):
    """Convert points to an SVG path string (M + L commands)."""
    parts = [f"M {points[0][0]},{points[0][1]}"]
    for x, y in points[1:]:
        parts.append(f"L {x},{y}")
    return " ".join(parts)


def write_svg(filepath, vw, vh, text=False, font_b64=None):
    """Write an SVG logo at the given dimensions.

    The waveform + dots are always vertically centered in the available space.
    When text=True, the waveform sits in the upper portion and the wordmark below.
    """
    margin_x = vw * MARGIN
    wave_cy = vh / 2 if not text else vh * 0.42

    dot_left = margin_x
    dot_right = vw - margin_x
    wave_start = dot_left + DOT_RADIUS
    wave_end = dot_right - DOT_RADIUS

    wave_points = sine_waveform(wave_start, wave_end, wave_cy)
    path_d = points_to_path(wave_points)

    # Scale stroke relative to canvas height (baseline: 512px)
    stroke = round(STROKE_WIDTH * (vh / 512), 2)
    dot_r = round(DOT_RADIUS * (vh / 512), 2)
    stroke = max(stroke, 2)
    dot_r = max(dot_r, 3)

    style_block = ""
    text_block = ""
    if text:
        font_size = round(vh * 0.063)
        text_y = round(vh * 0.82)
        style_block = f'''
  <defs><style>
    @font-face {{
      font-family: 'Jura';
      src: url('data:font/truetype;base64,{font_b64}') format('truetype');
      font-weight: 300;
    }}
  </style></defs>'''
        text_block = f'''
  <text x="{vw / 2}" y="{text_y}" text-anchor="middle" fill="{TEXT_COLOR}"
        font-family="'Jura', 'Helvetica Neue', sans-serif"
        font-size="{font_size}" font-weight="300" letter-spacing="0.28em"
        opacity="0.85">EssentiaTD</text>'''

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {vw} {vh}">{style_block}
  <rect width="{vw}" height="{vh}" fill="{BG_COLOR}"/>
  <circle cx="{dot_left}" cy="{wave_cy}" r="{dot_r}" fill="{ACCENT}"/>
  <circle cx="{dot_right}" cy="{wave_cy}" r="{dot_r}" fill="{ACCENT}"/>
  <path d="{path_d}" fill="none" stroke="{ACCENT}" stroke-width="{stroke}" stroke-linecap="round" stroke-linejoin="round"/>{text_block}
</svg>
'''
    with open(filepath, "w") as f:
        f.write(svg)
    print(f"Wrote {filepath} ({vw}x{vh})")


def write_jpg(filepath, vw, vh, text=False, font_path=None):
    """Render the logo directly to JPG using Pillow (no SVG conversion needed)."""
    from PIL import Image, ImageDraw, ImageFont

    bg = tuple(int(BG_COLOR.lstrip("#")[i:i+2], 16) for i in (0, 2, 4))
    accent = tuple(int(ACCENT.lstrip("#")[i:i+2], 16) for i in (0, 2, 4))
    text_col = tuple(int(TEXT_COLOR.lstrip("#")[i:i+2], 16) for i in (0, 2, 4))

    img = Image.new("RGB", (vw, vh), bg)
    draw = ImageDraw.Draw(img)

    margin_x = vw * MARGIN
    wave_cy = vh / 2 if not text else vh * 0.42

    dot_left = margin_x
    dot_right = vw - margin_x
    wave_start = dot_left + DOT_RADIUS
    wave_end = dot_right - DOT_RADIUS

    # Scale stroke and dot radius relative to canvas height
    stroke = max(round(STROKE_WIDTH * (vh / 512)), 2)
    dot_r = max(round(DOT_RADIUS * (vh / 512)), 3)

    # Draw dots
    draw.ellipse([dot_left - dot_r, wave_cy - dot_r,
                  dot_left + dot_r, wave_cy + dot_r], fill=accent)
    draw.ellipse([dot_right - dot_r, wave_cy - dot_r,
                  dot_right + dot_r, wave_cy + dot_r], fill=accent)

    # Draw waveform
    points = sine_waveform(wave_start, wave_end, wave_cy)
    xy = [(x, y) for x, y in points]
    draw.line(xy, fill=accent, width=stroke, joint="curve")

    # Draw text
    if text and font_path and os.path.exists(font_path):
        font_size = round(vh * 0.063)
        text_y = round(vh * 0.82)
        try:
            font = ImageFont.truetype(font_path, font_size)
        except OSError:
            font = ImageFont.load_default()
        label = "EssentiaTD"
        bbox = draw.textbbox((0, 0), label, font=font)
        tw = bbox[2] - bbox[0]
        draw.text(((vw - tw) / 2, text_y - font_size), label,
                  fill=text_col, font=font)

    img.save(filepath, "JPEG", quality=95)
    print(f"Wrote {filepath} ({vw}x{vh})")


if __name__ == "__main__":
    dir_ = os.path.dirname(os.path.abspath(__file__))
    font_b64 = load_font_base64()

    for name, w, h, text in OUTPUTS:
        path = os.path.join(dir_, name)
        if name.endswith(".svg"):
            write_svg(path, w, h, text=text,
                      font_b64=font_b64 if text else None)
        elif name.endswith(".jpg"):
            write_jpg(path, w, h, text=text, font_path=FONT_PATH)
        else:
            print(f"Skipped {name} — unsupported format (use .svg or .jpg)")
