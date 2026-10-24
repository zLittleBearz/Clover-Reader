#!/usr/bin/env python3
"""สร้างฟอนต์ไทยแบบบิตแมป (ทีละ cluster) -> src/thai_font.h
ใช้ Pillow + RAQM (HarfBuzz) จัดวางสระ/วรรณยุกต์ให้ถูกตำแหน่งตามฟอนต์จริง
รัน:  python3 tools/gen_thai.py [--font /path/font.otf] [--sizes 32,22] [--thresh 110]
"""
import argparse, os, sys, glob
from PIL import Image, ImageDraw, ImageFont, features

ap = argparse.ArgumentParser()
ap.add_argument("--font", default="")
ap.add_argument("--sizes", default="32,22")
ap.add_argument("--weights", default="400,500,700")
ap.add_argument("--thresh", type=int, default=110)
ap.add_argument("--out", default="src/thai_font.h")
a = ap.parse_args()

if not features.check("raqm"):
    sys.exit("Pillow ไม่มี RAQM -> รัน: sudo apt-get install -y libraqm0 แล้ว pip install -U pillow")

def find_font():
    if a.font: return a.font
    pats = ["tools/fonts/NotoSansThaiLooped*.ttf", "/usr/share/fonts/**/NotoSansThaiLooped*.ttf", "/usr/share/fonts/**/NotoSansThai*.ttf", "/usr/share/fonts/**/Loma.otf",
            "/usr/share/fonts/**/Loma.ttf", "/usr/share/fonts/**/Garuda.*", "/usr/share/fonts/**/Norasi.*"]
    for p in pats:
        r = sorted(glob.glob(p, recursive=True))
        if r: return r[0]
    sys.exit("ไม่พบฟอนต์ไทย -> sudo apt-get install -y fonts-tlwg-loma-otf")

FONT = find_font()
print("font:", FONT)
SIZES = [int(s) for s in a.sizes.split(",")]
WEIGHTS = [int(s) for s in a.weights.split(",")]

def load_font(size, weight):
    """คืน ImageFont ที่ตั้งน้ำหนักแล้ว (ฟอนต์ variable ใช้แกน wght; ฟอนต์ธรรมดา: >=600 ใช้ไฟล์ Bold ข้าง ๆ ถ้ามี)"""
    path = FONT
    f = ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.RAQM)
    try:
        axes = f.get_variation_axes()
    except Exception:
        axes = []
    if axes:
        vals = []
        for ax in axes:
            hi = ax["maximum"]
            if hi > 200:  vals.append(max(ax["minimum"], min(hi, weight)))   # แกนน้ำหนัก (100-900)
            else:         vals.append(ax["default"])                         # แกนความกว้าง ใช้ค่าปกติ
        f.set_variation_by_axes(vals)
    elif weight >= 600:
        for cand in (path.replace("Loma.otf", "Loma-Bold.otf"), path.replace("Regular", "Bold")):
            if cand != path and os.path.exists(cand):
                f = ImageFont.truetype(cand, size, layout_engine=ImageFont.Layout.RAQM); break
    return f


CONS = [c for c in range(0x0E01, 0x0E2F) if c not in (0x0E03, 0x0E05)]   # ไม่รวม ฃ ฅ
UPPER_LOWER = [0x0E31, 0x0E34, 0x0E35, 0x0E36, 0x0E37, 0x0E38, 0x0E39, 0x0E3A, 0x0E47, 0x0E4D]
VOWELS_T = [0x0E31, 0x0E34, 0x0E35, 0x0E36, 0x0E37, 0x0E38, 0x0E39, 0x0E3A, 0x0E47]
TONES = [0x0E48, 0x0E49, 0x0E4A, 0x0E4B, 0x0E4C]
SARA_AM = 0x0E33

def tkey(cps):
    k = 0x10000000
    for i, c in enumerate(cps):
        k |= (c - 0x0E00) << (7 * i)
    return k

clusters = []   # (key, string)
for c in range(0x20, 0x7F):
    clusters.append((c, chr(c)))
COMBINING = set(UPPER_LOWER + TONES + [0x0E4E])
for c in range(0x0E2F, 0x0E5C):
    if c in COMBINING or c in (0x0E3B, 0x0E3C, 0x0E3D, 0x0E3E, 0x0E4F, 0x0E5A, 0x0E5B) and False:
        continue
    if c in COMBINING: continue
    if 0x0E3B <= c <= 0x0E3E: continue
    clusters.append((tkey([c]), chr(c)))
for c in CONS:
    seqs = [[c]]
    seqs += [[c, m] for m in UPPER_LOWER]
    seqs += [[c, t] for t in TONES]
    seqs += [[c, m, t] for m in VOWELS_T for t in TONES]
    seqs += [[c, t, SARA_AM] for t in TONES[:4]]
    for s in seqs:
        clusters.append((tkey(s), "".join(chr(x) for x in s)))
# ฤ ฦ เป็นพยัญชนะอยู่แล้วใน CONS (0E24, 0E26) ; 0E2F-0E5B ที่เหลือใส่ไว้ด้านบน
clusters.sort(key=lambda t: t[0])

out = []
out.append("// สร้างโดย tools/gen_thai.py -- อย่าแก้ด้วยมือ\n#pragma once\n#include <stdint.h>\n")
out.append("#ifndef TG_DEFINED\n#define TG_DEFINED\nstruct TG { uint32_t key; uint32_t off; uint8_t adv, w, h; int8_t xoff, yoff; };\n"
           "struct TFont { const TG *tab; int n; const uint8_t *pool; int lh; int asc; };\n#endif\n")
fonts_decl = []
fi = 0
for wt in WEIGHTS:
  for S in SIZES:
    si = fi; fi += 1
    font = load_font(S, wt)
    asc, desc = font.getmetrics()
    pad = S
    H = (asc + desc) * 2
    base_y = asc + S           # baseline ใน canvas
    pool = bytearray(); tab = []
    for key, s in clusters:
        W = int(font.getlength(s)) + 2 * pad + 4
        img = Image.new("L", (W, H), 0)
        ImageDraw.Draw(img).text((pad, base_y), s, font=font, fill=255, anchor="ls")
        adv = int(round(font.getlength(s)))
        bb = img.point(lambda v: 255 if v >= a.thresh else 0).getbbox()
        if bb is None:
            tab.append((key, len(pool), adv, 0, 0, 0, 0)); continue
        x0, y0, x1, y1 = bb
        w, h = x1 - x0, y1 - y0
        bw = (w + 7) // 8
        off = len(pool)
        for y in range(y0, y1):
            row = [1 if img.getpixel((x, y)) >= a.thresh else 0 for x in range(x0, x1)]
            row += [0] * (bw * 8 - w)
            for i in range(bw):
                v = 0
                for b in range(8): v = (v << 1) | row[i * 8 + b]
                pool.append(v)
        tab.append((key, off, adv, w, h, x0 - pad, y0 - base_y))
    out.append(f"static const TG tf_tab{si}[] = {{\n")
    for (k, o, adv, w, h, xo, yo) in tab:
        out.append(f"{{0x{k:08X}u,{o}u,{adv},{w},{h},{xo},{yo}}},\n")
    out.append("};\n")
    out.append(f"static const uint8_t tf_pool{si}[] = {{\n")
    for i in range(0, len(pool), 32):
        out.append(",".join(str(v) for v in pool[i:i + 32]) + ",\n")
    if not pool: out.append("0\n")
    out.append("};\n")
    fonts_decl.append(f"{{tf_tab{si}, {len(tab)}, tf_pool{si}, {asc + desc}, {asc}}}")
    print(f"weight {wt} size {S}: {len(tab)} glyphs, pool {len(pool)} bytes, lh {asc+desc}")
out.append("#define TF_SIZES %d\n#define TF_WEIGHTS %d\n" % (len(SIZES), len(WEIGHTS)))
out.append("static const int TF_WEIGHT_VAL[TF_WEIGHTS] = { " + ", ".join(str(w) for w in WEIGHTS) + " };\n")
out.append("#define TF_N %d\n" % (len(SIZES) * len(WEIGHTS)))
out.append("static const TFont TF[TF_N] = { " + ", ".join(fonts_decl) + " };\n")
open(a.out, "w").write("".join(out))
print("wrote", a.out)

