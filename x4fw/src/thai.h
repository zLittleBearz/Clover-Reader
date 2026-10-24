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
