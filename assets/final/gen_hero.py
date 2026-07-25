"""1920x1080 release/hero card: new EssentiaTD identity + version badge + tagline.

Reuses the final mark + wordmark from gen_final. Run:
  python assets/final/gen_hero.py
"""
import os
from PIL import Image, ImageDraw, ImageFilter
import gen_final as F

HERE = os.path.dirname(os.path.abspath(__file__))
VERSION = "v2.0.1"
TAGLINE = "Real-time & offline audio analysis for TouchDesigner"
TAGLINE2 = "Free  ·  Windows & macOS"


def pill(base, cx, cy, text, fs, fill, border, textcol):
    # draw on its own layer so translucent fill actually blends (ImageDraw
    # writes alpha directly; on RGB export that flattens to solid otherwise).
    f = F.load_sg(fs)
    tw = f.getlength(text)
    padx, pady = fs * 0.9, fs * 0.45
    w, h = tw + 2 * padx, fs + 2 * pady
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    x0, y0 = cx - w / 2, cy - h / 2
    d.rounded_rectangle([x0, y0, x0 + w, y0 + h], radius=h / 2,
                        fill=fill, outline=border, width=max(2, fs // 15))
    d.text((cx, cy - fs * 0.06), text, font=f, fill=textcol, anchor="mm")
    base.alpha_composite(layer)


def line(base, cx, cy, text, fs, col):
    d = ImageDraw.Draw(base)
    d.text((cx, cy), text, font=F.load_sg(fs), fill=col, anchor="mm")


# Layouts. WIDE reproduces the original 1920x1080 card exactly; SQUARE is the
# 1:1 crop-safe variant for feeds that centre-crop wide images (Patreon, IG).
# Wave x extents follow gen_final's WIDE/LOCK lockups.
WIDE = dict(x0=0.31, x1=0.69, wave_y=0.35, amp=0.150, stroke=0.017,
            wbase=0.585, wtarget=0.25, pill_y=0.685, pill_fs=0.0240,
            tag_y=0.782, tag_fs=0.0185, tag2_y=0.842, tag2_fs=0.0150)
SQUARE = dict(x0=0.16, x1=0.84, wave_y=0.335, amp=0.155, stroke=0.020,
              wbase=0.575, wtarget=0.52, pill_y=0.680, pill_fs=0.0300,
              tag_y=0.790, tag_fs=0.0225, tag2_y=0.855, tag2_fs=0.0185)


def render(out, W=1920, H=1080, lay=WIDE):
    SS = F.SS
    W3, H3, D = W * SS, H * SS, min(W, H) * SS
    base = Image.new("RGBA", (W3, H3), F.BG + (255,))

    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    F.draw_wave(ImageDraw.Draw(mark), W3 * lay["x0"], W3 * lay["x1"],
                H3 * lay["wave_y"], amp=lay["amp"] * D, w=lay["stroke"] * D,
                ramp=(F.DEEP, F.BRIGHT), dotc=F.DOTC)
    glow = mark.filter(ImageFilter.GaussianBlur(radius=D * 0.013))
    a = glow.getchannel("A").point(lambda v: int(v * 0.55))
    glow.putalpha(a)
    base.alpha_composite(glow)
    base.alpha_composite(mark)

    F.draw_word_png(base, W3 / 2.0, H3 * lay["wbase"], lay["wtarget"] * W3,
                    F.WORDCOL)
    pill(base, W3 / 2.0, H3 * lay["pill_y"], VERSION, int(lay["pill_fs"] * H3),
         fill=(229, 72, 77, 34), border=(224, 74, 78, 235), textcol=(244, 150, 152))
    line(base, W3 / 2.0, H3 * lay["tag_y"], TAGLINE,
         int(lay["tag_fs"] * H3), (156, 156, 162))
    line(base, W3 / 2.0, H3 * lay["tag2_y"], TAGLINE2,
         int(lay["tag2_fs"] * H3), (108, 108, 114))

    base.resize((W, H), Image.LANCZOS).convert("RGB").save(out)
    print("wrote", out)


if __name__ == "__main__":
    render(os.path.join(HERE, "essentiatd-release-1920x1080.png"))
    render(os.path.join(HERE, "essentiatd-release-1080x1080.png"),
           1080, 1080, SQUARE)
