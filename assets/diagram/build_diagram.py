"""Build the 'How EssentiaTD works' operator diagram as a self-contained HTML.

Fonts are base64-embedded (no load-timing issues in headless render). The header
waveform mark is computed from the same Gaussian-enveloped sine as the logo.
Render with Chrome/Edge headless (see render step).
"""
import base64
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.normpath(os.path.join(HERE, "..", "fonts"))


def b64(name):
    with open(os.path.join(FONTS, name), "rb") as f:
        return base64.b64encode(f.read()).decode("ascii")


SG = b64("SpaceGrotesk.ttf")
SORA = b64("Sora.ttf")


def wave_path(x0, x1, yc, amp, cycles=3, n=140):
    xr = x1 - x0
    mid = (x0 + x1) / 2.0
    sigma = xr / 5.0
    pts = []
    for i in range(n + 1):
        t = i / n
        x = x0 + t * xr
        env = math.exp(-((x - mid) ** 2) / (2 * sigma ** 2))
        y = yc - env * amp * math.sin(2 * math.pi * cycles * t)
        pts.append((round(x, 1), round(y, 1)))
    d = "M " + " L ".join(f"{x},{y}" for x, y in pts)
    return d, pts[0], pts[-1]

MARK_W, MARK_H = 132, 46
mark_d, mp0, mp1 = wave_path(10, MARK_W - 10, MARK_H / 2, 15)


def bez(x1, y1, x2, y2, k=0.45):
    dx = (x2 - x1) * k
    return f"M {x1},{y1} C {x1+dx},{y1} {x2-dx},{y2} {x2},{y2}"


# ---- fixed layout coordinates (canvas 1600 x 1040) ----
SPEC = (610, 427)          # spectrum right-center (fan-out origin)
CARDS = {"spectral": 251, "tonal": 403, "rhythm": 555}   # left-center y at x=910
AUDIO_R = (272, 505)       # audio right-center
LOUD_L = (910, 764)

connectors = []
# audio -> spectrum
connectors.append((bez(AUDIO_R[0], AUDIO_R[1], 360, 427, 0.5), "solid"))
# spectrum -> three analyzers
for y in CARDS.values():
    connectors.append((bez(SPEC[0], SPEC[1], 910, y, 0.5), "solid"))
# audio -> loudness (raw audio, routed low, dashed)
connectors.append((
    f"M {AUDIO_R[0]},{AUDIO_R[1]+18} C {AUDIO_R[0]+180},{AUDIO_R[1]+230} "
    f"{LOUD_L[0]-320},{LOUD_L[1]} {LOUD_L[0]},{LOUD_L[1]}", "dash"))

paths_svg = ""
for d, kind in connectors:
    if kind == "dash":
        paths_svg += (f'<path d="{d}" fill="none" stroke="#e5484d" '
                      f'stroke-width="2.4" stroke-opacity="0.5" '
                      f'stroke-dasharray="7 7" marker-end="url(#ar)"/>')
    else:
        paths_svg += (f'<path d="{d}" fill="none" stroke="#e5484d" '
                      f'stroke-width="2.6" stroke-opacity="0.8" '
                      f'marker-end="url(#ar)"/>')


def card(x, y, name, tag, chips, cls="card"):
    ch = "".join(f'<span class="chip">{c}</span>' for c in chips)
    return (f'<div class="{cls}" style="left:{x}px;top:{y}px">'
            f'<div class="cn">{name}</div><div class="ct">{tag}</div>'
            f'<div class="chips">{ch}</div></div>')


nodes = ""
# audio node
nodes += (f'<div class="audio" style="left:60px;top:435px">'
          f'<svg width="{MARK_W}" height="{MARK_H}" viewBox="0 0 {MARK_W} {MARK_H}">'
          f'<circle cx="{mp0[0]}" cy="{mp0[1]}" r="4" fill="#e5484d"/>'
          f'<circle cx="{mp1[0]}" cy="{mp1[1]}" r="4" fill="#e5484d"/>'
          f'<path d="{mark_d}" fill="none" stroke="#e5484d" stroke-width="3" '
          f'stroke-linecap="round" stroke-linejoin="round"/></svg>'
          f'<div class="an">Audio In</div>'
          f'<div class="asub">any Audio&nbsp;CHOP</div></div>')
# spectrum hub
nodes += (f'<div class="hub" style="left:360px;top:350px">'
          f'<div class="cn">Spectrum</div>'
          f'<div class="ct">FFT magnitude</div>'
          f'<div class="hubsub">computed once,<br>shared by the three</div></div>')
# analyzer cards
nodes += card(910, 185, "Spectral", "Timbre",
              ["MFCC", "Centroid", "Flux", "Rolloff", "Mel bands", "HFC"])
nodes += card(910, 337, "Tonal", "Pitch &amp; harmony",
              ["Pitch", "HPCP chroma", "Key / Scale", "Dissonance"])
nodes += card(910, 489, "Rhythm", "Time",
              ["Onset", "BPM", "Beat phase", "Confidence"])
nodes += card(910, 700, "Loudness", "Level &middot; raw audio",
              ["LUFS (R128)", "RMS", "Zero-crossing"], cls="card loud")

html = f"""<!doctype html><html><head><meta charset="utf-8"><style>
@font-face {{ font-family:'SG'; src:url(data:font/ttf;base64,{SG}) format('truetype'); }}
@font-face {{ font-family:'Sora'; src:url(data:font/ttf;base64,{SORA}) format('truetype'); }}
* {{ margin:0; padding:0; box-sizing:border-box; }}
html,body {{ width:1600px; height:1040px; }}
body {{
  font-family:'Sora',sans-serif;
  background:
    radial-gradient(1200px 700px at 46% 42%, #16161a 0%, #0c0c0e 62%);
  color:#e6e6ea; overflow:hidden; position:relative;
}}
.stage {{ position:absolute; inset:0; }}
.brand {{ position:absolute; left:60px; top:44px; display:flex; align-items:center; gap:16px; }}
.brand .name {{ font-family:'SG'; font-weight:500; font-size:26px; letter-spacing:.22em; color:#e9e9ee; }}
h1 {{ position:absolute; left:60px; top:104px; font-family:'SG'; font-weight:600;
     font-size:44px; letter-spacing:.005em; color:#f2f2f4; }}
.sub {{ position:absolute; left:62px; top:160px; font-size:18px; color:#9a9aa2; letter-spacing:.01em; }}
svg.wires {{ position:absolute; inset:0; z-index:1; }}
.audio,.hub,.card {{ position:absolute; z-index:2; border-radius:16px; }}
.audio {{ width:212px; height:150px; background:#151518; border:1px solid #2a2a30;
  display:flex; flex-direction:column; align-items:center; justify-content:center; gap:8px; }}
.audio .an {{ font-family:'SG'; font-size:22px; letter-spacing:.03em; color:#e9e9ee; }}
.audio .asub {{ font-size:14px; color:#83838b; }}
.hub {{ width:250px; height:155px; background:rgba(229,72,77,.10);
  border:1.5px solid rgba(229,72,77,.55); padding:22px 24px;
  box-shadow:0 0 40px rgba(229,72,77,.10); }}
.hub .hubsub {{ margin-top:10px; font-size:14.5px; line-height:1.4; color:#c9979a; }}
.card {{ width:630px; height:132px; background:#151518; border:1px solid #26262c;
  border-left:3px solid #e5484d; padding:18px 24px; }}
.card.loud {{ border-left-color:#e5484d; background:#141416; }}
.cn {{ font-family:'SG'; font-weight:600; font-size:25px; letter-spacing:.02em; color:#f0f0f2; }}
.ct {{ font-size:14px; color:#8b8b93; margin-top:2px; text-transform:uppercase; letter-spacing:.14em; }}
.chips {{ margin-top:14px; display:flex; flex-wrap:wrap; gap:8px; }}
.chip {{ font-family:'Sora'; font-size:14.5px; color:#d2d2d8; background:#202025;
  border:1px solid #2c2c33; border-radius:8px; padding:5px 11px; }}
.hub .ct {{ margin-top:2px; }}
.footer {{ position:absolute; left:60px; right:60px; bottom:40px; z-index:2;
  display:flex; align-items:center; gap:20px; }}
.mode {{ flex:1; background:#151518; border:1px solid #26262c; border-radius:14px;
  padding:16px 20px; display:flex; gap:14px; align-items:flex-start; }}
.mode .dot {{ width:12px; height:12px; border-radius:50%; margin-top:5px; flex:none; }}
.mode.rt .dot {{ background:#e5484d; }}
.mode.bt .dot {{ background:#f6a24a; }}
.mode .mt {{ font-family:'SG'; font-size:17px; letter-spacing:.03em; color:#ececf0; }}
.mode .md {{ font-size:14px; color:#9a9aa2; margin-top:3px; line-height:1.35; }}
.tag {{ position:absolute; right:64px; bottom:200px; z-index:2; font-size:15px;
  color:#8f8f97; }}
.tag b {{ color:#e5484d; font-weight:600; }}
.rawlbl {{ position:absolute; z-index:2; left:360px; top:690px; font-size:13px;
  color:#9c6d6f; letter-spacing:.08em; text-transform:uppercase; }}
</style></head><body>
<div class="stage">
  <div class="brand">
    <svg width="{MARK_W}" height="{MARK_H}" viewBox="0 0 {MARK_W} {MARK_H}">
      <circle cx="{mp0[0]}" cy="{mp0[1]}" r="4" fill="#e5484d"/>
      <circle cx="{mp1[0]}" cy="{mp1[1]}" r="4" fill="#e5484d"/>
      <path d="{mark_d}" fill="none" stroke="#e5484d" stroke-width="3"
        stroke-linecap="round" stroke-linejoin="round"/>
    </svg>
    <span class="name">EssentiaTD</span>
  </div>
  <h1>How the operators work</h1>
  <div class="sub">Five analysis operators. Real-time or offline. Every result is a CHOP channel.</div>

  <svg class="wires" viewBox="0 0 1600 1040">
    <defs>
      <marker id="ar" markerWidth="9" markerHeight="9" refX="7" refY="4.2"
        orient="auto"><path d="M0,0 L8,4.2 L0,8.4 z" fill="#e5484d"/></marker>
    </defs>
    {paths_svg}
  </svg>

  <div class="rawlbl">raw audio</div>
  {nodes}

  <div class="footer">
    <div class="mode rt"><div class="dot"></div><div>
      <div class="mt">Realtime</div>
      <div class="md">Per-frame at TD's cook rate. One sample per channel, live.</div></div></div>
    <div class="mode bt"><div class="dot"></div><div>
      <div class="mt">Batch</div>
      <div class="md">Whole file, offline on a background thread. N frames; each operator runs its own FFT.</div></div></div>
  </div>
</div>
</body></html>"""

out = os.path.join(HERE, "operators-diagram.html")
with open(out, "w", encoding="utf-8") as f:
    f.write(html)
print("wrote", out)
