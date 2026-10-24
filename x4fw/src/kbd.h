#pragma once
// คีย์บอร์ดบนจอ ไทย/อังกฤษ/ตัวเลข-สัญลักษณ์ (ลูกศร=เลื่อน, OK=พิมพ์)
#define K_SH "\001"
#define K_BS "\002"
#define K_LG "\003"
#define K_SY "\004"
#define K_SP "\005"
#define K_OK "\006"
#define K_AB "\007"
#define K_S2 "\016"

// เลเยอร์: 0=EN เล็ก 1=EN ใหญ่ 2=ตัวเลข/สัญลักษณ์ 3=ไทย 4=ไทย Shift 5=สัญลักษณ์ 2
static const char *const KBL[6][5] = {
  { "q w e r t y u i o p", "a s d f g h j k l",
    K_SH " z x c v b n m " K_BS, K_SY " " K_LG " " K_SP " . " K_OK, nullptr },
  { "Q W E R T Y U I O P", "A S D F G H J K L",
    K_SH " Z X C V B N M " K_BS, K_SY " " K_LG " " K_SP " . " K_OK, nullptr },
  { "1 2 3 4 5 6 7 8 9 0", "@ # $ % & * - + ( )",
    K_S2 " ! \" ' : ; / ? " K_BS, K_AB " " K_LG " " K_SP " . " K_OK, nullptr },
  { "ๅ / - ภ ถ \u0E38 \u0E36 ค ต จ ข ช",
    "ๆ ไ ำ พ ะ \u0E31 \u0E35 ร น ย บ ล",
    "ฟ ห ก ด เ \u0E49 \u0E48 า ส ว ง " K_BS,
    K_SH " ผ ป แ อ \u0E34 \u0E37 ท ม ใ ฝ",
    K_SY " " K_LG " " K_SP " . " K_OK },
  { "+ ๑ ๒ ๓ ๔ \u0E39 ฿ ๕ ๖ ๗ ๘ ๙",
    "๐ \" ฎ ฑ ธ \u0E4D \u0E4A ณ ฯ ญ ฐ ,",
    "ฤ ฆ ฏ โ ฌ \u0E47 \u0E4B ษ ศ ซ . " K_BS,
    K_SH " ( ) ฉ ฮ \u0E3A \u0E4C ? ฒ ฬ ฦ",
    K_SY " " K_LG " " K_SP " . " K_OK },
  { "[ ] { } < > ^ ~ ` |", "\\ _ = , . ; : / ? !",
    K_S2 " ( ) - + ' \" " K_BS, K_AB " " K_LG " " K_SP " . " K_OK, nullptr }
};
static const char *const KBNAME[6] = {
  "อังกฤษ  abc", "อังกฤษ  ABC", "ตัวเลข / สัญลักษณ์", "ไทย", "ไทย  Shift", "สัญลักษณ์ 2"
};
#define KB_KH 70
#define KB_Y0 296

struct KbKey { int x, w, n; const char *t; };
static KbKey kbk[16];
static int kbN = 0;
static uint32_t kbLastBs = 0;

static int kbRows(int layer) { int r = 0; while (r < 5 && KBL[layer][r]) r++; return r; }
static int kbWt(const char *t) {
  switch ((uint8_t)t[0]) {
    case 5: return 5;
    case 1: case 2: case 3: case 4: case 6: case 7: case 14: return 2;
    default: return 1;
  }
}
static void kbParse(int layer, int row) {
  const char *s = KBL[layer][row];
  kbN = 0;
  int tw = 0;
  while (*s && kbN < 16) {
    while (*s == ' ') s++;
    if (!*s) break;
    const char *e = s;
    while (*e && *e != ' ') e++;
    kbk[kbN].t = s; kbk[kbN].n = (int)(e - s); kbk[kbN].w = kbWt(s);
    tw += kbk[kbN].w; kbN++;
    s = e;
  }
  int cum = 0;
  for (int i = 0; i < kbN; i++) {
    int x0 = cum * 480 / tw;
    cum += kbk[i].w;
    int x1 = cum * 480 / tw;
    kbk[i].x = x0; kbk[i].w = x1 - x0;
  }
}
static bool kbIsMark(const char *t, int n) {
  if (n != 3 || (uint8_t)t[0] != 0xE0) return false;
  uint32_t cp = ((uint32_t)((uint8_t)t[0] & 0x0F) << 12) | ((uint32_t)((uint8_t)t[1] & 0x3F) << 6) | ((uint8_t)t[2] & 0x3F);
  return cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E3A) || (cp >= 0x0E47 && cp <= 0x0E4E);
}
static void kbDropFirst(String &s) {
  int j = 1;
  while (j < (int)s.length() && (((uint8_t)s[j]) & 0xC0) == 0x80) j++;
  s = s.substring(j);
}
static void kbBackspace(String &s) {
  int i = s.length();
  if (i == 0) return;
  i--;
  while (i > 0 && (((uint8_t)s[i]) & 0xC0) == 0x80) i--;
  s.remove(i);
  kbLastBs = millis();
}
// คืน fi (0=ตัวใหญ่, 1=ตัวเล็ก) และเขียนข้อความปุ่มลง out
static int kbLabel(const KbKey &k, char *out, int lang, int layer) {
  uint8_t c0 = (uint8_t)k.t[0];
  if (c0 < 0x20) {
    const char *s = "";
    switch (c0) {
      case 1: s = lang ? "Shift" : "Aa"; break;
      case 2: s = "ลบ"; break;
      case 3: s = lang ? "EN" : "ไทย"; break;
      case 4: s = "123"; break;
      case 5: s = "เว้นวรรค"; break;
      case 6: s = "เสร็จ"; break;
      case 7: s = lang ? "กขค" : "abc"; break;
      case 14: s = (layer == 5) ? "123" : "#+="; break;
    }
    strcpy(out, s);
    return 1;
  }
  int o = 0;
  if (kbIsMark(k.t, k.n)) { strcpy(out, "ก"); o = 3; }
  memcpy(out + o, k.t, k.n);
  out[o + k.n] = 0;
  return 0;
}
// วาดปุ่มสระ/วรรณยุกต์: วงกลมจุด ๆ + เครื่องหมาย (วาดจาก "ก"+เครื่องหมาย แล้วลบตัว ก ออก)
static void kbDrawMark(const char *t, int x, int w, int base) {
  const TFont &f = TF[thI(0)];
  uint32_t mk = ((uint32_t)((uint8_t)t[0] & 0x0F) << 12) | ((uint32_t)((uint8_t)t[1] & 0x3F) << 6) | ((uint8_t)t[2] & 0x3F);
  uint32_t c1[1] = {0x0E01};
  uint32_t c2[2] = {0x0E01, mk};
  uint32_t c3[1] = {mk};
  const TG *gc = thFind(f, thKey(c1, 1));
  const TG *gm = thFind(f, thKey(c2, 2));
  if (!gc) return;
  int x0 = x + (w - gc->adv) / 2;
  if (gm && gm->w) {
    cv->drawBitmap(x0 + gm->xoff, base + gm->yoff, f.pool + gm->off, gm->w, gm->h, 1);
    if (gc->w) cv->drawBitmap(x0 + gc->xoff, base + gc->yoff, f.pool + gc->off, gc->w, gc->h, 0);
  } else {
    const TG *gl = thFind(f, thKey(c3, 1));
    if (gl && gl->w) cv->drawBitmap(x0 + gl->xoff, base + gl->yoff, f.pool + gl->off, gl->w, gl->h, 1);
  }
  int cx = x0 + gc->xoff + gc->w / 2, cy = base + gc->yoff + gc->h / 2;
  int r = (gc->w > gc->h ? gc->w : gc->h) / 2 + 1;
  for (int a = 0; a < 12; a++) {
    float th = a * 0.5236f;
    cv->fillRect(cx + (int)lroundf(r * cosf(th)) - 1, cy + (int)lroundf(r * sinf(th)) - 1, 2, 2, 1);
  }
}
static void kbDraw(const char *title, const String &hint, const String &text, int layer, int lang, int row, int ci) {
  header(title);
  cv->setTextColor(1);
  if (hint.length()) thWrapDraw(16, 128, hint.c_str(), 1, 448, 1);
  cv->drawRoundRect(8, 140, 464, 98, 8, 1);
  cv->drawRoundRect(9, 141, 462, 96, 7, 1);
  // แสดงเฉพาะท้ายข้อความที่พอดีช่อง
  String body = text;
  int cnt = 0, i = body.length();
  while (i > 0 && cnt < 40) { i--; if ((((uint8_t)body[i]) & 0xC0) != 0x80) cnt++; }
  bool cut = i > 0;
  body = body.substring(i);
  if (cut) { while (body.length() >= 3 && kbIsMark(body.c_str(), 3)) kbDropFirst(body); }
  String disp;
  for (;;) {
    String d = (cut ? String("..") : String("")) + body + "_";
    if (thWidth(d.c_str(), 0) <= 432 || body.length() == 0) { disp = d; break; }
    kbDropFirst(body); cut = true;
    while (body.length() >= 3 && kbIsMark(body.c_str(), 3)) kbDropFirst(body);
  }
  thDraw(18, 200, disp.c_str(), 0);
  thDraw(16, 272, KBNAME[layer], 1);
  int nr = kbRows(layer);
  for (int r = 0; r < nr; r++) {
    kbParse(layer, r);
    int y = KB_Y0 + r * KB_KH;
    for (int k = 0; k < kbN; k++) {
      int x = kbk[k].x, w = kbk[k].w;
      bool sel = (r == row && k == ci);
      if (sel) grayTile(x + 3, y + 3, w - 6, KB_KH - 6, 8);
      cv->drawRoundRect(x + 2, y + 2, w - 4, KB_KH - 4, 8, 1);
      if (sel) {
        cv->drawRoundRect(x + 3, y + 3, w - 6, KB_KH - 6, 7, 1);
        cv->drawRoundRect(x + 4, y + 4, w - 8, KB_KH - 8, 6, 1);
      }
      if (kbIsMark(kbk[k].t, kbk[k].n)) { kbDrawMark(kbk[k].t, x, w, y + KB_KH / 2 + 11); continue; }
      char lab[40];
      int fi = kbLabel(kbk[k], lab, lang, layer);
      int tw = thWidth(lab, fi);
      thDraw(x + (w - tw) / 2, y + KB_KH / 2 + (fi ? 8 : 11), lab, fi);
    }
  }
  thWrapDraw(16, 752, "BACK=ลบ (ถ้าช่องว่าง = ยกเลิก)", 1, 448, 1);
  thWrapDraw(16, 782, "OK=พิมพ์   POWER=สลับไทย/EN", 1, 448, 1);
}
static bool kbInputImpl(const char *title, String &text, int maxBytes, int lang, const String &hint) {
  int layer = lang ? 3 : 0, row = 0, ci = 0, cx;
  qclear();
  kbParse(layer, row);
  cx = kbk[0].x + kbk[0].w / 2;
  while (true) {
    kbParse(layer, row);
    if (ci < 0 || ci >= kbN) ci = 0;
    kbDraw(title, hint, text, layer, lang, row, ci);
    kbParse(layer, row);
    present();
    int b = waitBtn();
    bool relay = false;
    if (b == LEFT || b == RIGHT) {
      ci = (ci + (b == RIGHT ? 1 : -1) + kbN) % kbN;
      cx = kbk[ci].x + kbk[ci].w / 2;
    } else if (b == UP || b == DOWN) {
      int nr = kbRows(layer);
      row = (row + (b == DOWN ? 1 : -1) + nr) % nr;
      relay = true;
    } else if (b == BACK) {
      if (text.length() == 0) {
        if (millis() - kbLastBs < 1500) continue;   // ลบจนหมดจากการกดค้าง ยังไม่ยกเลิก
        return false;
      }
      kbBackspace(text);
    } else if (b == POWER) {
      lang ^= 1; layer = lang ? 3 : 0; relay = true;
    } else if (b == CONFIRM) {
      uint8_t c0 = (uint8_t)kbk[ci].t[0];
      int tn = kbk[ci].n;
      if (tn < 1) tn = 1;
      if (tn > 7) tn = 7;
      char tk[8];
      memcpy(tk, kbk[ci].t, tn); tk[tn] = 0;
      if (c0 >= 0x20) {
        if ((int)text.length() + tn <= maxBytes) text += tk;
        if (layer == 1) { layer = 0; relay = true; }
        else if (layer == 4) { layer = 3; relay = true; }
      } else {
        switch (c0) {
          case 1: layer = (layer == 0) ? 1 : (layer == 1) ? 0 : (layer == 3) ? 4 : (layer == 4) ? 3 : layer; relay = true; break;
          case 2: kbBackspace(text); break;
          case 3: lang ^= 1; layer = lang ? 3 : 0; relay = true; break;
          case 4: layer = 2; relay = true; break;
          case 5: if ((int)text.length() + 1 <= maxBytes) text += ' '; break;
          case 6: DBG("kb:ok"); return true;
          case 7: layer = lang ? 3 : 0; relay = true; break;
          case 14: layer = (layer == 2) ? 5 : 2; relay = true; break;
        }
      }
    }
    if (relay) {
      int nr = kbRows(layer);
      if (row >= nr) row = nr - 1;
      kbParse(layer, row);
      ci = kbN - 1;
      for (int i = 0; i < kbN; i++) if (cx >= kbk[i].x && cx < kbk[i].x + kbk[i].w) { ci = i; break; }
    }
  }
}
// คืน true = กด "เสร็จ", false = ยกเลิก (text ใช้เป็นค่าเริ่มต้นและรับผลลัพธ์)
static bool kbInput(const char *title, String &text, int maxBytes, int lang = 0, const String &hint = "") {
  kbBackRep = true;
  bool r = kbInputImpl(title, text, maxBytes, lang, hint);
  DBG("kb:ret");
  kbBackRep = false;
  return r;
}
static void kbTest() {
  String t = "";
  if (kbInput("ทดสอบคีย์บอร์ด", t, 120, 0, "พิมพ์แล้วเลือก เสร็จ"))
    msg("ข้อความที่พิมพ์", t.length() ? t : String("(ว่าง)"));
}
