"""1920x1080 release/hero card: new EssentiaTD identity + version badge + tagline.

Reuses the final mark + wordmark from gen_final. Run:
  python assets/final/gen_hero.py
"""
import os
from PIL import Image, ImageDraw, ImageFilter
import gen_final as F

HERE = os.path.dirname(os.path.abspath(__file__))
VERSION = "v1.1.8-beta"
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


def render(out):
    W, H, SS = 1920, 1080, F.SS
    W3, H3, D = W * SS, H * SS, min(W, H) * SS
    base = Image.new("RGBA", (W3, H3), F.BG + (255,))

    mark = Image.new("RGBA", (W3, H3), (0, 0, 0, 0))
    F.draw_wave(ImageDraw.Draw(mark), W3 * 0.31, W3 * 0.69, H3 * 0.35,
                amp=0.15 * D, w=0.017 * D, ramp=(F.DEEP, F.BRIGHT), dotc=F.DOTC)
    glow = mark.filter(ImageFilter.GaussianBlur(radius=D * 0.013))
    a = glow.getchannel("A").point(lambda v: int(v * 0.55))
    glow.putalpha(a)
    base.alpha_composite(glow)
    base.alpha_composite(mark)

    F.draw_word_png(base, W3 / 2.0, H3 * 0.585, 0.25 * W3, F.WORDCOL)
    pill(base, W3 / 2.0, H3 * 0.685, VERSION, int(0.024 * H3),
         fill=(229, 72, 77, 34), border=(224, 74, 78, 235), textcol=(244, 150, 152))
    line(base, W3 / 2.0, H3 * 0.782, TAGLINE, int(0.0185 * H3), (156, 156, 162))
    line(base, W3 / 2.0, H3 * 0.842, TAGLINE2, int(0.015 * H3), (108, 108, 114))

    base.resize((W, H), Image.LANCZOS).convert("RGB").save(out)
    print("wrote", out)


if __name__ == "__main__":
    render(os.path.join(HERE, "essentiatd-release-1920x1080.png"))
