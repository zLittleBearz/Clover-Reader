#!/bin/bash
# x4fw v0.4b: ภาษาไทย + ฟอนต์ Noto Sans Thai Looped (Regular/Medium/Bold) + หน้า "ทดสอบฟอนต์" ใน Apps
# (ใช้แทน x4fw_v04.sh ได้เลย ไม่ต้องรันตัวนั้น)
# อัปโหลดไฟล์นี้เข้าโฟลเดอร์ x4fw แล้วรัน: bash x4fw_v04b.sh
set -e
[ -f x4fw/platformio.ini ] && cd x4fw
[ -f platformio.ini ] || { echo "ไม่พบ platformio.ini กรุณา cd ไปที่ x4fw ก่อน"; exit 1; }
[ -f src/icons.h ] || { echo "ไม่พบ src/icons.h ต้องรัน x4fw_ui_v2.sh ก่อน"; exit 1; }
grep -q 'define VERSION "v0\.' src/main.cpp || { echo "src/main.cpp ไม่ใช่ตัวที่คาดไว้ ยกเลิก (ไม่แก้อะไร)"; exit 1; }
mkdir -p tools/fonts
cp src/main.cpp "src/main.cpp.bak-$(date +%H%M%S)"
echo "== ติดตั้งเครื่องมือ (ครั้งแรกอาจนาน) =="
(sudo apt-get update -qq && sudo apt-get install -y -qq fonts-tlwg-loma-otf libraqm0) || echo "(apt ไม่สำเร็จ ข้ามไป)"
pip install -q -U pillow 2>/dev/null || pip install -q -U --break-system-packages pillow
echo "== ดาวน์โหลด Noto Sans Thai Looped =="
F=tools/fonts/NotoSansThaiLooped.ttf
if [ ! -s "$F" ]; then
  for U in "https://raw.githubusercontent.com/google/fonts/main/ofl/notosansthailooped/NotoSansThaiLooped%5Bwdth%2Cwght%5D.ttf" \
           "https://cdn.jsdelivr.net/gh/google/fonts@main/ofl/notosansthailooped/NotoSansThaiLooped%5Bwdth%2Cwght%5D.ttf"; do
    if curl -fsSL --retry 2 -o "$F.tmp" "$U" && [ "$(wc -c < "$F.tmp")" -gt 100000 ]; then mv "$F.tmp" "$F"; break; fi
    rm -f "$F.tmp"
  done
fi
if [ -s "$F" ]; then echo "ได้ฟอนต์: $F"; else echo "!! ดาวน์โหลด Noto ไม่ได้ จะใช้ Loma แทนชั่วคราว (ส่งข้อความนี้มาให้ฉันดู)"; fi
cat > tools/gen_thai.py <<'X4EOF_GEN'
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

X4EOF_GEN
cat > src/thai.h <<'X4EOF_THAI'
#pragma once
// ข้อความไทย+ละติน: แยก UTF-8 -> cluster -> บิตแมป (thai_font.h) / ตัดบรรทัด / วาดลง canvas `cv`
#include <stdint.h>
#include <string.h>
#include "thai_font.h"

#define TH_MAXC 300
#ifndef DEFAULT_WEIGHT
#define DEFAULT_WEIGHT 0   // 0=Regular 1=Medium 2=Bold (ลำดับตาม --weights)
#endif
static int thWeight = DEFAULT_WEIGHT;
static inline int thI(int fi) { return thWeight * TF_SIZES + fi; }
struct ThCl { uint16_t start, len; const TG *g; uint8_t adv; bool noBreakBefore; bool space; };

static inline uint32_t thDecode(const uint8_t *s, int n, int &used) {
  uint8_t c = s[0];
  if (c < 0x80) { used = 1; return c; }
  if ((c & 0xE0) == 0xC0 && n >= 2) { used = 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F); }
  if ((c & 0xF0) == 0xE0 && n >= 3) { used = 3; return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
  used = 1; return '?';
}
static inline bool thIsCons(uint32_t c) { return c >= 0x0E01 && c <= 0x0E2E; }
static inline bool thIsMark(uint32_t c) {
  return c == 0x0E31 || (c >= 0x0E34 && c <= 0x0E3A) || (c >= 0x0E47 && c <= 0x0E4E);
}
static inline bool thIsTone(uint32_t c) { return c >= 0x0E48 && c <= 0x0E4B; }
static inline bool thIsLead(uint32_t c) { return c >= 0x0E40 && c <= 0x0E44; }
static inline bool thNoBreakBefore(uint32_t c) {   // ห้ามขึ้นบรรทัดใหม่ด้วยตัวเหล่านี้
  return c == 0x0E30 || c == 0x0E32 || c == 0x0E33 || c == 0x0E2F || c == 0x0E45 || c == 0x0E46 || thIsMark(c);
}
static inline uint32_t thKey(const uint32_t *cp, int n) {
  if (n == 1 && cp[0] < 0x0E00) return cp[0];
  uint32_t k = 0x10000000;
  for (int i = 0; i < n && i < 4; i++) k |= (cp[i] - 0x0E00) << (7 * i);
  return k;
}
static const TG *thFind(const TFont &f, uint32_t key) {
  int lo = 0, hi = f.n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t k = f.tab[mid].key;
    if (k == key) return &f.tab[mid];
    if (k < key) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}
static int thSpaceW(const TFont &f) { const TG *g = thFind(f, ' '); return g ? g->adv : 8; }

// แยกเป็น cluster; คืนจำนวน
static int thSegment(const char *str, int fi, ThCl *out, int maxn) {
  const TFont &f = TF[thI(fi)];
  const uint8_t *s = (const uint8_t *)str;
  int n = (int)strlen(str), i = 0, cnt = 0;
  bool prevLead = false, prevWord = false;
  while (i < n && cnt < maxn) {
    int u; uint32_t cp[4]; int nc = 0; int st = i;
    cp[nc++] = thDecode(s + i, n - i, u); i += u;
    if (thIsCons(cp[0])) {
      while (i < n && nc < 3) {
        int u2; uint32_t c2 = thDecode(s + i, n - i, u2);
        if (!thIsMark(c2)) break;
        cp[nc++] = c2; i += u2;
      }
      if (nc >= 2 && thIsTone(cp[nc - 1]) && nc < 4 && i < n) {      // พยัญชนะ+วรรณยุกต์+ำ
        int u2; uint32_t c2 = thDecode(s + i, n - i, u2);
        if (c2 == 0x0E33) { cp[nc++] = c2; i += u2; }
      }
    }
    const TG *g = nullptr; int k = nc;
    while (k >= 1 && !(g = thFind(f, thKey(cp, k)))) k--;         // ถ้าไม่เจอ ตัดเครื่องหมายท้ายออกทีละตัว
    if (!g) g = thFind(f, '?');
    ThCl &c = out[cnt++];
    c.start = st; c.len = i - st; c.g = g; c.adv = g ? g->adv : 0;
    c.space = (nc == 1 && cp[0] == ' ');
    bool word = cp[0] < 0x80 && cp[0] != ' ' && cp[0] != '_' && cp[0] != '-' && cp[0] != '/';
    c.noBreakBefore = (cnt == 1) || prevLead || thNoBreakBefore(cp[0]) || (word && prevWord);
    prevLead = thIsLead(cp[0]); prevWord = word;
  }
  return cnt;
}
static int thWidth(const char *str, int fi) {
  static ThCl cl[TH_MAXC];
  int n = thSegment(str, fi, cl, TH_MAXC), w = 0;
  for (int i = 0; i < n; i++) w += cl[i].adv;
  return w;
}
// วาดบรรทัดเดียว: (x, baseline)
static int thDrawRange(int x, int base, const char *str, const ThCl *cl, int a, int b, int fi) {
  const TFont &f = TF[thI(fi)];
  for (int i = a; i < b; i++) {
    const TG *g = cl[i].g;
    if (g && g->w) cv->drawBitmap(x + g->xoff, base + g->yoff, f.pool + g->off, g->w, g->h, 1);
    x += cl[i].adv;
  }
  return x;
}
static int thDraw(int x, int base, const char *str, int fi) {
  static ThCl cl[TH_MAXC];
  int n = thSegment(str, fi, cl, TH_MAXC);
  return thDrawRange(x, base, str, cl, 0, n, fi);
}
static inline int thPitch(int fi) { return (TF[thI(fi)].lh * 4) / 5; }   // ระยะบรรทัด

// ตัดบรรทัดตามความกว้าง (px) ไม่เกิน maxLines; บรรทัดสุดท้ายถ้าล้นใส่ ".." ; คืนจำนวนบรรทัด
// วาดเลย: วางบรรทัดแรกที่ baseline = base
static int thWrapDraw(int x, int base, const char *str, int fi, int maxw, int maxLines, bool draw = true) {
  static ThCl cl[TH_MAXC];
  int n = thSegment(str, fi, cl, TH_MAXC);
  int lines = 0, ls = 0;
  const int dotsW = 2 * (thFind(TF[thI(fi)], '.') ? thFind(TF[thI(fi)], '.')->adv : 6);
  while (ls < n && lines < maxLines) {
    while (ls < n && cl[ls].space && lines > 0) ls++;            // ตัดช่องว่างต้นบรรทัดที่ต่อมา
    if (ls >= n) break;
    int w = 0, i = ls, brk = -1;
    for (; i < n; i++) {
      if (w + cl[i].adv > maxw && i > ls) break;
      w += cl[i].adv;
      if (i + 1 < n && !cl[i + 1].noBreakBefore) brk = i + 1;
      if (cl[i].space && i + 1 < n) brk = i + 1;
      if (i + 1 < n && (str[cl[i].start] == '_' || str[cl[i].start] == '-' || str[cl[i].start] == '/')) brk = i + 1;
    }
    int e;
    if (i >= n) e = n;                                           // เหลือพอดีบรรทัด
    else e = (brk > ls) ? brk : i;
    bool last = (lines == maxLines - 1);
    if (last && e < n) {                                         // ล้น: ตัดให้พอใส่ ".."
      int ww = 0; e = ls;
      while (e < n && ww + cl[e].adv + dotsW <= maxw) { ww += cl[e].adv; e++; }
      if (draw) {
        int xe = thDrawRange(x, base + lines * thPitch(fi), str, cl, ls, e, fi);
        thDraw(xe, base + lines * thPitch(fi), "..", fi);
      }
      lines++; break;
    }
    int de = e; while (de > ls && cl[de - 1].space) de--;
    if (draw) thDrawRange(x, base + lines * thPitch(fi), str, cl, ls, de, fi);
    lines++; ls = e;
  }
  return lines;
}
X4EOF_THAI
cat > src/main.cpp <<'X4EOF_MAIN'
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include <Adafruit_GFX.h>
#include "icons.h"

#define VERSION "v0.4b " __DATE__ " " __TIME__
#define EPD_SCK 8
#define EPD_MISO 7
#define EPD_MOSI 10
#define EPD_CS 21
#define EPD_DC 4
#define EPD_RST 5
#define EPD_BUSY 6
#define SD_CS 12
#define BTN_PWR 3
#define ROTATION 3   // ถ้าจอกลับหัว เปลี่ยนเป็น 1
#define PW 800
#define PH 480

// ---- refresh / UI tuning ----
#define FULL_EVERY 10        // partial กี่ครั้งแล้วค่อยล้างจอเต็ม (กันภาพซ้อน)
#define PARTIAL_CTRL2 0xFC   // SSD1677 fast/partial LUT (ถ้าภาพเพี้ยน ดูหมายเหตุ)
#define HILITE_50 0          // 0 = เทาอ่อน (25%), 1 = เทากลาง (50%)
#define FLIP_ROWS 1          // 1 = แก้ภาพกลับซ้าย-ขวา (ส่งแถว RAM กลับด้าน) ถ้ากลับอีกให้เป็น 0
#define SWAP_LR 0            // 1 = สลับปุ่มซ้าย/ขวา
#define SWAP_UD 0            // 1 = สลับปุ่มขึ้น/ลง

#include <string.h>
#include <strings.h>
#include <stdio.h>

enum Btn { NONE = -1, BACK, CONFIRM, LEFT, RIGHT, UP, DOWN, POWER };
static GFXcanvas1 *cv;
#include "thai.h"   // ข้อความไทย/ละติน (ฟอนต์จาก tools/gen_thai.py)
static SPISettings epdSpi(20000000, MSBFIRST, SPI_MODE0);
static int lastA1 = 0, lastA2 = 0;
static uint8_t prevBuf[PW * PH / 8];   // เฟรมก่อนหน้า (ใช้ทำ partial refresh)
static int partialCount = 0;
static bool firstDraw = true;

// ---------- EPD (SSD1677) ----------
static void waitBusy() {
  uint32_t t = millis();
  while (digitalRead(EPD_BUSY) == HIGH && millis() - t < 15000) delay(5);
}
static void cmd(uint8_t c) {
  SPI.beginTransaction(epdSpi);
  digitalWrite(EPD_DC, LOW); digitalWrite(EPD_CS, LOW);
  SPI.transfer(c);
  digitalWrite(EPD_CS, HIGH); SPI.endTransaction();
}
static void dat(uint8_t d) {
  SPI.beginTransaction(epdSpi);
  digitalWrite(EPD_DC, HIGH); digitalWrite(EPD_CS, LOW);
  SPI.transfer(d);
  digitalWrite(EPD_CS, HIGH); SPI.endTransaction();
}
static void sendRam(uint8_t c, const uint8_t *b) {
  cmd(0x4E); dat(0); dat(0);
  cmd(0x4F); dat(0); dat(0);
  cmd(c);
  uint8_t tmp[100];
  SPI.beginTransaction(epdSpi);
  digitalWrite(EPD_DC, HIGH); digitalWrite(EPD_CS, LOW);
  for (int r = 0; r < PH; r++) {
#if FLIP_ROWS
    const uint8_t *src = b + (PH - 1 - r) * 100;
#else
    const uint8_t *src = b + r * 100;
#endif
    for (int i = 0; i < 100; i++) tmp[i] = ~src[i];
    SPI.writeBytes(tmp, 100);
  }
  digitalWrite(EPD_CS, HIGH); SPI.endTransaction();
}
static void epdInit() {
  digitalWrite(EPD_RST, LOW); delay(20);
  digitalWrite(EPD_RST, HIGH); delay(20);
  waitBusy();
  cmd(0x12); delay(10); waitBusy();
  cmd(0x18); dat(0x80);
  cmd(0x0C); dat(0xAE); dat(0xC7); dat(0xC3); dat(0xC0); dat(0x80);
  cmd(0x01); dat((PH - 1) & 0xFF); dat((PH - 1) >> 8); dat(0x02);
  cmd(0x3C); dat(0x01);
  cmd(0x11); dat(0x03);
  cmd(0x44); dat(0); dat(0); dat((PW - 1) & 0xFF); dat((PW - 1) >> 8);
  cmd(0x45); dat(0); dat(0); dat((PH - 1) & 0xFF); dat((PH - 1) >> 8);
}
// แสดงผลจาก canvas: partial (ไม่กระพริบ) เป็นหลัก, full ทุก FULL_EVERY ครั้ง
static void present(bool forceFull = false) {
  bool full = forceFull || firstDraw || partialCount >= FULL_EVERY;
  const uint8_t *b = cv->getBuffer();
  epdInit();
  if (full) {
    sendRam(0x24, b);
    sendRam(0x26, b);
    cmd(0x22); dat(0xF7);
    cmd(0x20); waitBusy();
    partialCount = 0;
    firstDraw = false;
  } else {
    sendRam(0x24, b);          // ภาพใหม่
    sendRam(0x26, prevBuf);    // ภาพเก่า
    cmd(0x22); dat(PARTIAL_CTRL2);
    cmd(0x20); waitBusy();
    cmd(0x22); dat(0x03);      // ปิด analog/clock หลัง partial
    cmd(0x20); waitBusy();
    partialCount++;
  }
  memcpy(prevBuf, b, sizeof(prevBuf));
  cmd(0x10); dat(0x01);  // deep sleep
}

// ---------- Buttons ----------
static int readBtn() {
  if (digitalRead(BTN_PWR) == LOW) return POWER;
  lastA1 = analogRead(1);
  lastA2 = analogRead(2);
  if (lastA1 < 3800) {
    if (lastA1 >= 3100) return BACK;
    if (lastA1 >= 2090) return CONFIRM;
    if (lastA1 >= 750) return SWAP_LR ? RIGHT : LEFT;
    return SWAP_LR ? LEFT : RIGHT;
  }
  if (lastA2 < 3800) {
    bool up = lastA2 >= 1120;
    return (up != (bool)SWAP_UD) ? UP : DOWN;
  }
  return NONE;
}
static int waitBtn() {
  int b;
  while ((b = readBtn()) == NONE) delay(10);
  delay(40);
  if (readBtn() != b) return waitBtn();
  while (readBtn() != NONE) delay(10);
  return b;
}

// ---------- UI helpers ----------
static void header(const char *t) {
  cv->fillScreen(0);
  cv->setTextColor(1);
  thWrapDraw(16, 64, t, 0, 448, 1);
  cv->fillRect(0, 90, 480, 4, 1);
}
static void footer(const char *t) {
  cv->setTextColor(1);
  thWrapDraw(16, 782, t, 1, 340, 1);
}
static void msg(const char *title, const String &a, const String &b = "") {
  header(title);
  thWrapDraw(16, 170, a.c_str(), 0, 448, 2);
  thWrapDraw(16, 290, b.c_str(), 0, 448, 2);
  footer("กดปุ่มใดก็ได้");
  present();
  waitBtn();
}
// ไฮไลท์สีเทา = ลายจุด (dither) ทำงานกับ partial refresh ได้เร็วและไม่กระพริบ
static void grayTile(int x, int y, int w, int h, int r) {
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      int cx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
      int cy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
      if (cx * cx + cy * cy > r * r) continue;
      int px = x + i, py = y + j;
#if HILITE_50
      if (((px + py) & 1) == 0) cv->drawPixel(px, py, 1);
#else
      if ((px & 1) == 0 && (py & 1) == 0) cv->drawPixel(px, py, 1);
#endif
    }
  }
}

// เมนูรายการ: ตัวใหญ่ ขึ้นได้ 2 บรรทัด ไฮไลท์เทา คืนค่า index หรือ -1 (BACK)
static int menu(const char *title, String *items, int n, int ts, const char *foot, int sel0 = 0) {
  const int ROWS = 6, RH = 100, Y0 = 110;
  (void)ts;
  int sel = sel0 < 0 ? 0 : (sel0 >= n ? n - 1 : sel0);
  while (true) {
    header(title);
    int first = sel - sel % ROWS;
    for (int i = 0; i < ROWS && first + i < n; i++) {
      int y = Y0 + i * RH;
      bool s = (first + i == sel);
      if (s) grayTile(8, y, 464, RH - 12, 12);
      cv->setTextColor(1);
      const char *t = items[first + i].c_str();
      int lines = thWrapDraw(0, 0, t, 0, 436, 2, false);
      int base0 = y + ((RH - 12) - (lines - 1) * thPitch(0)) / 2 + 11;
      thWrapDraw(20, base0, t, 0, 436, 2);
    }
    footer(foot);
    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", sel + 1, n);
    thDraw(480 - 16 - thWidth(pg, 1), 782, pg, 1);
    present();
    int b = waitBtn();
    if ((b == UP || b == LEFT) && sel > 0) sel--;
    else if ((b == DOWN || b == RIGHT) && sel < n - 1) sel++;
    else if (b == CONFIRM || b == POWER) return sel;
    else if (b == BACK) return -1;
  }
}

// ---------- Library: เลือกดูไฟล์/โฟลเดอร์ใน SD ----------
#define LIB_MAX 128
static String libNames[LIB_MAX];
static String libLabels[LIB_MAX];
static bool libDirs[LIB_MAX];

static int listDir(const String &path, int maxn) {
  File root = SD.open(path.c_str());
  if (!root || !root.isDirectory()) return -1;
  int n = 0;
  while (n < maxn) {
    File e = root.openNextFile();
    if (!e) break;
    String nm = e.name();
    bool d = e.isDirectory();
    e.close();
    if (nm.startsWith(".") || nm == "System Volume Information") continue;
    libNames[n] = nm; libDirs[n] = d; n++;
  }
  root.close();
  return n;
}
static void sortEntries(int n) {   // โฟลเดอร์ก่อน แล้วเรียงชื่อ (ไม่สนตัวพิมพ์)
  for (int i = 1; i < n; i++) {
    String nm = libNames[i]; bool d = libDirs[i];
    int j = i - 1;
    while (j >= 0) {
      bool after = (libDirs[j] != d) ? (!libDirs[j] && d)
                                     : (strcasecmp(libNames[j].c_str(), nm.c_str()) > 0);
      if (!after) break;
      libNames[j + 1] = libNames[j]; libDirs[j + 1] = libDirs[j];
      j--;
    }
    libNames[j + 1] = nm; libDirs[j + 1] = d;
  }
}
static String leafOf(const String &p) {
  int i = p.lastIndexOf('/');
  return i < 0 ? p : p.substring(i + 1);
}
static void fileScreen(const String &dir, const String &name) {
  String full = (dir == "/" ? String("/") : dir + "/") + name;
  size_t sz = 0;
  File f = SD.open(full.c_str(), FILE_READ);
  if (f) { sz = f.size(); f.close(); }
  char sb[24];
  if (sz >= 1024 * 1024) snprintf(sb, sizeof(sb), "%.1f MB", sz / 1048576.0);
  else snprintf(sb, sizeof(sb), "%u KB", (unsigned)((sz + 1023) / 1024));
  header("ไฟล์");
  cv->setTextColor(1);
  thWrapDraw(16, 160, name.c_str(), 0, 448, 4);
  thDraw(16, 350, sb, 0);
  thDraw(16, 420, "ตัวอ่าน: เร็ว ๆ นี้", 0);
  footer("กดปุ่มใดก็ได้");
  present();
  waitBtn();
}
static void library() {
  SD.end();
  if (!SD.begin(SD_CS, SPI, 20000000)) { msg("ห้องสมุด", "อ่าน SD การ์ดไม่ได้"); return; }
  String cwd = "/";
  int selStack[16];
  int depth = 0, sel = 0;
  while (true) {
    int n = listDir(cwd, LIB_MAX);
    if (n < 0) { msg("ห้องสมุด", "เปิดโฟลเดอร์นี้ไม่ได้"); return; }
    if (n == 0) {
      msg("ห้องสมุด", "โฟลเดอร์ว่างเปล่า");
      if (depth == 0) return;
      int i = cwd.lastIndexOf('/');
      cwd = i <= 0 ? String("/") : cwd.substring(0, i);
      sel = selStack[--depth];
      continue;
    }
    sortEntries(n);
    for (int i = 0; i < n; i++) libLabels[i] = libDirs[i] ? libNames[i] + "/" : libNames[i];
    String title = depth == 0 ? String("ห้องสมุด") : leafOf(cwd);
    int s = menu(title.c_str(), libLabels, n, 4, "OK=เปิด  BACK=ย้อนกลับ", sel);
    if (s < 0) {
      if (depth == 0) return;
      int i = cwd.lastIndexOf('/');
      cwd = i <= 0 ? String("/") : cwd.substring(0, i);
      sel = selStack[--depth];
    } else if (libDirs[s]) {
      if (depth >= 15) { msg("ห้องสมุด", "โฟลเดอร์ลึกเกินไป"); continue; }
      selStack[depth++] = s;
      cwd = (cwd == "/" ? String("/") : cwd + "/") + libNames[s];
      sel = 0;
    } else {
      sel = s;
      fileScreen(cwd, libNames[s]);
    }
  }
}

// ---------- Home: แถบไอคอน 4 ตัวด้านล่าง ----------
#define N_ICONS 4
#define CELL_W 120
#define TILE 96
#define TILE_Y 668
static const uint8_t *const ICONS[N_ICONS] = {ICON_LIBRARY, ICON_APPS, ICON_UPDATE, ICON_SETTING};
static const char *const LABELS[N_ICONS] = {"ห้องสมุด", "แอป", "อัปเดต", "ตั้งค่า"};

static void drawHome(int sel) {
  cv->fillScreen(0);
  cv->setTextColor(1);
  thDraw(24, 76, "X4 Reader", 0);
  cv->fillRect(0, 100, 480, 4, 1);
  cv->setTextSize(1);
  cv->setCursor(24, 114);
  cv->print(VERSION);
  for (int i = 0; i < N_ICONS; i++) {
    int tx = i * CELL_W + (CELL_W - TILE) / 2;
    if (i == sel) grayTile(tx, TILE_Y, TILE, TILE, 14);
    cv->drawBitmap(tx + (TILE - 64) / 2, TILE_Y + (TILE - 64) / 2, ICONS[i], 64, 64, 1);
    cv->setTextColor(1);
    thDraw(i * CELL_W + (CELL_W - thWidth(LABELS[i], 1)) / 2, TILE_Y + TILE + 28, LABELS[i], 1);
  }
}

// ---------- Firmware update from SD ----------
static int listBins(String *out, int maxn) {
  int n = 0;
  File root = SD.open("/");
  if (!root) return -1;
  while (n < maxn) {
    File e = root.openNextFile();
    if (!e) break;
    String nm = e.name();
    bool dir = e.isDirectory();
    e.close();
    String l = nm; l.toLowerCase();
    if (!dir && l.endsWith(".bin") && !nm.startsWith(".")) out[n++] = nm;
  }
  root.close();
  return n;
}
static bool flashFile(const String &path, String &err) {
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) { err = "Cannot open file"; return false; }
  size_t sz = f.size();
  if (sz < 64 * 1024) { err = "File too small"; f.close(); return false; }
  int m = f.read(); f.seek(0);
  if (m != 0xE9) { err = "Not an app .bin"; f.close(); return false; }
  if (!Update.begin(sz)) { err = "No OTA slot"; f.close(); return false; }
  size_t w = Update.writeStream(f);
  f.close();
  if (w != sz) { Update.abort(); err = "Write failed"; return false; }
  if (!Update.end(true)) { err = "Verify failed"; return false; }
  return true;
}
static void updateMenu() {
  SD.end();
  if (!SD.begin(SD_CS, SPI, 20000000)) { msg("อัปเดต", "อ่าน SD การ์ดไม่ได้"); return; }
  static String files[16];
  int n = listBins(files, 16);
  if (n <= 0) { msg("อัปเดต", "ไม่พบไฟล์ .bin ใน SD", "(วางไว้นอกสุด ไม่อยู่ในโฟลเดอร์)"); return; }
  int s = menu("เลือกไฟล์ .bin", files, n, 3, "OK=แฟลช  BACK=ยกเลิก");
  if (s < 0) return;
  String path = "/" + files[s];
  header("กำลังแฟลช...");
  thDraw(16, 170, "ห้ามปิดเครื่อง", 0);
  present();
  String err;
  if (!flashFile(path, err)) { msg("ล้มเหลว", err); return; }
  SD.rename(path.c_str(), (path + ".done").c_str());
  header("เสร็จแล้ว");
  thDraw(16, 170, "กำลังรีสตาร์ต...", 0);
  present(true);
  ESP.restart();
}

// แสดงค่าปุ่มที่ "เพิ่งกด" (อ่านตอนกดค้าง ไม่ใช่ตอนปล่อย)
static void buttonTest() {
  const char *names[] = {"BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN", "POWER"};
  int b = NONE, a1 = 0, a2 = 0;
  while (true) {
    header("Button test");
    cv->setTextSize(3);
    cv->setCursor(16, 130); cv->print(b < 0 ? "-" : names[b]);
    cv->setCursor(16, 230); cv->print("ADC1: "); cv->print(a1);
    cv->setCursor(16, 280); cv->print("ADC2: "); cv->print(a2);
    footer("POWER = ออก");
    present();
    while ((b = readBtn()) == NONE) delay(10);
    a1 = lastA1; a2 = lastA2;
    while (readBtn() != NONE) delay(10);
    delay(40);
    if (b == POWER) return;
  }
}
// เทียบน้ำหนักฟอนต์ (Regular / Medium / Bold) บนจอจริง: ซ้าย/ขวา = เลือก, BACK = ออก (ค่าที่เลือกใช้กับทั้ง UI จนปิดเครื่อง)
static const char *weightName(int v) {
  return v <= 300 ? "Light" : v <= 400 ? "Regular" : v <= 500 ? "Medium" : v <= 600 ? "SemiBold" : "Bold";
}
static void fontTest() {
  int sel = thWeight;
  while (true) {
    header("ทดสอบฟอนต์");
    for (int w = 0; w < TF_WEIGHTS; w++) {
      int y0 = 110 + w * 210;
      thWeight = w;
      char lb[40];
      snprintf(lb, sizeof(lb), "%s %d%s", weightName(TF_WEIGHT_VAL[w]), TF_WEIGHT_VAL[w], w == sel ? "  <" : "");
      thWrapDraw(16, y0 + 24, lb, 1, 440, 1);
      thWrapDraw(16, y0 + 76, "ห้องสมุด ตั้งค่า น้ำ ปู่", 0, 448, 1);
      thWrapDraw(16, y0 + 124, "เด็กน้อยอ่านหนังสือเรื่องผู้ชายที่ปีนเขา Fourth_Wing", 1, 448, 2);
      if (w == sel) cv->fillRect(0, y0 - 4, 6, 196, 1);
    }
    thWeight = sel;
    footer("ซ้าย/ขวา=เลือก  BACK=ออก");
    present();
    int b = waitBtn();
    if ((b == UP || b == LEFT) && sel > 0) sel--;
    else if ((b == DOWN || b == RIGHT) && sel < TF_WEIGHTS - 1) sel++;
    else if (b == BACK || b == CONFIRM || b == POWER) { thWeight = sel; return; }
  }
}
static void appsMenu() {
  String items[2] = {"Button test", "ทดสอบฟอนต์"};
  int s = menu("แอป", items, 2, 3, "OK=เปิด  BACK=กลับ");
  if (s == 0) buttonTest();
  else if (s == 1) fontTest();
}

static void openIcon(int i) {
  if (i == 0) library();
  else if (i == 1) appsMenu();
  else if (i == 2) updateMenu();
  else if (i == 3) msg("ตั้งค่า", "เร็ว ๆ นี้");
}
static void home() {
  int sel = 0;
  while (true) {
    drawHome(sel);
    present();
    int b = waitBtn();
    if (b == LEFT || b == UP) sel = (sel + N_ICONS - 1) % N_ICONS;
    else if (b == RIGHT || b == DOWN) sel = (sel + 1) % N_ICONS;
    else if (b == CONFIRM || b == POWER) openIcon(sel);
  }
}

void setup() {
  pinMode(EPD_CS, OUTPUT); digitalWrite(EPD_CS, HIGH);
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_RST, OUTPUT); digitalWrite(EPD_RST, HIGH);
  pinMode(EPD_BUSY, INPUT);
  pinMode(BTN_PWR, INPUT);
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, -1);
  cv = new GFXcanvas1(PW, PH);
  cv->setRotation(ROTATION);
  uint32_t t = millis();
  while (readBtn() != NONE && millis() - t < 3000) delay(10);
}

void loop() {
  home();
}
X4EOF_MAIN
echo "== สร้างฟอนต์ไทย 3 น้ำหนัก (src/thai_font.h) ใช้เวลาสักครู่ =="
python3 tools/gen_thai.py --sizes 32,22 --weights 400,500,700
echo "OK -> รัน: pio run   (แล้วแฟลช .bin; ในเครื่อง: Apps > ทดสอบฟอนต์)"
