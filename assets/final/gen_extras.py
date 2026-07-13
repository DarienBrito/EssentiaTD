"""Extra brand formats: favicon, GitHub social card, mono (1-color) versions,
and the README banner (overwrites ../banner.png).

Run:  python assets/final/gen_extras.py
"""
import os
from PIL import Image
import gen_final as F

HERE = os.path.dirname(os.path.abspath(__file__))


def p(n):
    return os.path.join(HERE, n)


# 2:1 GitHub social card layout
SOCIAL = dict(cy=0.40, x0=0.31, x1=0.69, amp=0.15, stroke=0.017,
              wbase=0.70, wtarget=0.30, svg_word_px=0.05)
# favicon: bold mark filling the tile, no wordmark
FAV = dict(cy=0.50, x0=0.10, x1=0.90, amp=0.23, stroke=0.048, wbase=0, wtarget=0)

MW = (238, 238, 238)   # mono white
MB = (20, 20, 22)      # mono black


if __name__ == "__main__":
    # GitHub social preview (upload manually in repo Settings > Social preview)
    F.render_png(1280, 640, SOCIAL, out=p("essentiatd-github-social-1280x640.png"))

    # favicon.ico (multi-size, dark tile + bold red wave)
    fav = F.render_png(256, 256, FAV, word=False, bg=F.BG)
    fav.save(p("favicon.ico"), format="ICO",
             sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("wrote favicon.ico")

    # mono / 1-color lockups + icons (flat color, no gradient/glow, transparent)
    for col, tag in ((MW, "white"), (MB, "black")):
        F.render_png(2048, 2048, F.LOCK, ramp=(col, col), dotc=col, wordcol=col,
                     glow=False, transparent=True, out=p(f"essentiatd-mono-{tag}.png"))
        F.render_png(2048, 2048, F.ICON, word=False, ramp=(col, col), dotc=col,
                     glow=False, transparent=True,
                     out=p(f"essentiatd-icon-mono-{tag}.png"))

    # README banner (deploy): clean wide lockup at 1920x1080 -> ../banner.png
    F.render_png(1920, 1080, F.WIDE, out=os.path.normpath(os.path.join(HERE, "..", "banner.png")))
    print("wrote ../banner.png (README banner)")

    for f in sorted(os.listdir(HERE)):
        if f.startswith(("essentiatd-github", "essentiatd-mono", "essentiatd-icon-mono", "favicon")):
            print("  ", f)
