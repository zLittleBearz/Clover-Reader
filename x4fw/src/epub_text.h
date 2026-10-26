#pragma once
// epub_text.h : แปลง XHTML -> ข้อความล้วน (สตรีม) + ตัวช่วยอ่าน OPF / พาธใน zip
// ไม่พึ่ง Arduino (ทดสอบบนคอมได้) ต้องใช้คู่กับ epub.h (ZSink)
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// ---------- XHTML -> text ----------
struct HtmlTxt {
  ZSink out; void *octx;
  uint8_t ob[1024]; int on;
  uint32_t written;
  bool failed;
  // ตัวแยกวิเคราะห์
  uint8_t st;                 // 0 ข้อความ, 1 แท็ก, 2 คอมเมนต์, 3 entity
  char tn[12]; uint8_t tl; bool tclose, tnameDone, tslash;
  char quote;
  char skipTag[8]; bool skip;
  char ent[12]; uint8_t el;
  uint8_t u8[4]; uint8_t u8n, u8need;
  bool pendSpace, atLineStart;
  uint8_t c1, c2;
};

static void htFlush(HtmlTxt *h) {
  if (h->on > 0) {
    if (!h->failed && !h->out(h->ob, h->on, h->octx)) h->failed = true;
    h->written += h->on;
  }
  h->on = 0;
}
static inline void htOut(HtmlTxt *h, uint8_t b) {
  h->ob[h->on++] = b;
  if (h->on >= (int)sizeof(h->ob)) htFlush(h);
}
static void htNewline(HtmlTxt *h) {
  h->pendSpace = false;
  if (!h->atLineStart) { htOut(h, '\n'); h->atLineStart = true; }
}
static void htRaw(HtmlTxt *h, uint32_t cp) {   // เขียนอักขระ (UTF-8) หลังเช็คช่องว่างค้าง
  if (h->pendSpace) { htOut(h, ' '); h->pendSpace = false; }
  if (cp < 0x80) htOut(h, (uint8_t)cp);
  else if (cp < 0x800) { htOut(h, 0xC0 | (cp >> 6)); htOut(h, 0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { htOut(h, 0xE0 | (cp >> 12)); htOut(h, 0x80 | ((cp >> 6) & 0x3F)); htOut(h, 0x80 | (cp & 0x3F)); }
  else { htOut(h, 0xF0 | (cp >> 18)); htOut(h, 0x80 | ((cp >> 12) & 0x3F)); htOut(h, 0x80 | ((cp >> 6) & 0x3F)); htOut(h, 0x80 | (cp & 0x3F)); }
  h->atLineStart = false;
}
static void htCp(HtmlTxt *h, uint32_t cp) {
  switch (cp) {
    case 0x20: case 0x09: case 0x0A: case 0x0D: case 0x0B: case 0x0C: case 0xA0:
    case 0x2002: case 0x2003: case 0x2009: case 0x200A: case 0x3000:
      if (!h->atLineStart) h->pendSpace = true;
      return;
    case 0x200B: case 0x200C: case 0x200D: case 0x2060: case 0xFEFF: case 0xAD: return;   // ตัวไม่แสดงผล
    case 0x2018: case 0x2019: htRaw(h, '\''); return;
    case 0x201C: case 0x201D: htRaw(h, '"'); return;
    case 0x2013: case 0x2014: htRaw(h, '-'); return;
    case 0x2026: htRaw(h, '.'); htRaw(h, '.'); htRaw(h, '.'); return;
    default: break;
  }
  if (cp < 0x20) return;
  htRaw(h, cp);
}
static bool htIs(const char *a, const char *b) { return strcmp(a, b) == 0; }
static bool htBlock(const char *n) {
  static const char *const B[] = {"p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6", "li", "tr", "blockquote", "section",
                                  "article", "ul", "ol", "table", "hr", "pre", "dt", "dd", "body", "figure", "figcaption", "aside", "nav", "header", "footer"};
  for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++) if (htIs(n, B[i])) return true;
  return false;
}
static void htTagDone(HtmlTxt *h) {
  h->tn[h->tl] = 0;
  for (int i = 0; i < h->tl; i++) if (h->tn[i] >= 'A' && h->tn[i] <= 'Z') h->tn[i] += 32;
  if (h->skip) {
    if (h->tclose && htIs(h->tn, h->skipTag)) h->skip = false;
    return;
  }
  if (!h->tclose && !h->tslash && (htIs(h->tn, "head") || htIs(h->tn, "style") || htIs(h->tn, "script") || htIs(h->tn, "title") || htIs(h->tn, "rt"))) {
    h->skip = true;
    size_t k = strlen(h->tn); if (k > sizeof(h->skipTag) - 1) k = sizeof(h->skipTag) - 1;
    memcpy(h->skipTag, h->tn, k); h->skipTag[k] = 0;
    return;
  }
  if (htBlock(h->tn)) htNewline(h);
  else if (htIs(h->tn, "td") || htIs(h->tn, "th")) { if (!h->atLineStart) h->pendSpace = true; }
}
static void htEntity(HtmlTxt *h) {
  h->ent[h->el] = 0;
  const char *e = h->ent;
  uint32_t cp = 0;
  if (e[0] == '#') {
    if (e[1] == 'x' || e[1] == 'X') cp = (uint32_t)strtoul(e + 2, nullptr, 16);
    else cp = (uint32_t)strtoul(e + 1, nullptr, 10);
  } else if (htIs(e, "amp")) cp = '&';
  else if (htIs(e, "lt")) cp = '<';
  else if (htIs(e, "gt")) cp = '>';
  else if (htIs(e, "quot")) cp = '"';
  else if (htIs(e, "apos")) cp = '\'';
  else if (htIs(e, "nbsp")) cp = 0xA0;
  else if (htIs(e, "ndash")) cp = 0x2013;
  else if (htIs(e, "mdash")) cp = 0x2014;
  else if (htIs(e, "hellip")) cp = 0x2026;
  else if (htIs(e, "lsquo") || htIs(e, "rsquo")) cp = 0x2019;
  else if (htIs(e, "ldquo") || htIs(e, "rdquo")) cp = 0x201C;
  if (cp && cp < 0x110000) htCp(h, cp);
}
static void htBegin(HtmlTxt *h) {   // เริ่มไฟล์ XHTML ใหม่ (คงสถานะบรรทัดไว้)
  h->st = 0; h->tl = 0; h->tclose = h->tnameDone = h->tslash = false; h->quote = 0; h->skip = false;
  h->el = 0; h->u8n = h->u8need = 0; h->c1 = h->c2 = 0;
  htNewline(h);
}
static void htInit(HtmlTxt *h, ZSink out, void *ctx) {
  memset(h, 0, sizeof(*h));
  h->out = out; h->octx = ctx; h->atLineStart = true;
}
static void htByte(HtmlTxt *h, uint8_t c) {
  switch (h->st) {
    case 1: {   // ในแท็ก
      if (h->quote) { if (c == (uint8_t)h->quote) h->quote = 0; return; }
      if (c == '>') { h->st = 0; htTagDone(h); return; }
      if (c == '"' || c == '\'') { h->quote = c; h->tslash = false; return; }
      if (c == '/') { if (h->tl == 0 && !h->tclose) { h->tclose = true; return; } h->tslash = true; h->tnameDone = true; return; }
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { if (h->tl) h->tnameDone = true; return; }
      h->tslash = false;
      if (!h->tnameDone && h->tl < sizeof(h->tn) - 1) {
        h->tn[h->tl++] = c;
        if (h->tl == 3 && h->tn[0] == '!' && h->tn[1] == '-' && h->tn[2] == '-') { h->st = 2; h->c1 = h->c2 = 0; }
      }
      return;
    }
    case 2:     // คอมเมนต์ <!-- ... -->
      if (c == '>' && h->c1 == '-' && h->c2 == '-') h->st = 0;
      h->c1 = h->c2; h->c2 = c;
      return;
    case 3: {   // entity
      if (c == ';') { h->st = 0; if (!h->skip) htEntity(h); return; }
      bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '#';
      if (ok && h->el < sizeof(h->ent) - 1) { h->ent[h->el++] = c; return; }
      h->st = 0;                      // ไม่ใช่ entity จริง: ถือเป็นข้อความ '&' ธรรมดา
      if (!h->skip) { htCp(h, '&'); for (int i = 0; i < h->el; i++) htCp(h, (uint8_t)h->ent[i]); }
      htByte(h, c);
      return;
    }
    default: break;
  }
  if (h->u8need) {   // ต่อ UTF-8
    if ((c & 0xC0) == 0x80) {
      h->u8[h->u8n++] = c;
      if (h->u8n == h->u8need) {
        uint32_t cp = 0;
        if (h->u8need == 2) cp = ((h->u8[0] & 0x1F) << 6) | (h->u8[1] & 0x3F);
        else if (h->u8need == 3) cp = ((h->u8[0] & 0x0F) << 12) | ((h->u8[1] & 0x3F) << 6) | (h->u8[2] & 0x3F);
        else cp = ((h->u8[0] & 0x07) << 18) | ((h->u8[1] & 0x3F) << 12) | ((h->u8[2] & 0x3F) << 6) | (h->u8[3] & 0x3F);
        h->u8need = 0; h->u8n = 0;
        if (!h->skip) htCp(h, cp);
      }
      return;
    }
    h->u8need = 0; h->u8n = 0;       // ลำดับเสีย: ทิ้งแล้วประมวลผลไบต์นี้ใหม่
  }
  if (c == '<') { h->st = 1; h->tl = 0; h->tclose = h->tnameDone = h->tslash = false; h->quote = 0; return; }
  if (c == '&') { h->st = 3; h->el = 0; return; }
  if (h->skip) return;
  if (c < 0x80) { htCp(h, c); return; }
  if ((c & 0xE0) == 0xC0) { h->u8[0] = c; h->u8n = 1; h->u8need = 2; }
  else if ((c & 0xF0) == 0xE0) { h->u8[0] = c; h->u8n = 1; h->u8need = 3; }
  else if ((c & 0xF8) == 0xF0) { h->u8[0] = c; h->u8n = 1; h->u8need = 4; }
}
static bool htSink(const uint8_t *p, size_t n, void *ctx) {   // ใช้เป็น ZSink ของ zipExtract
  HtmlTxt *h = (HtmlTxt *)ctx;
  for (size_t i = 0; i < n; i++) htByte(h, p[i]);
  return !h->failed;
}
static bool htFinish(HtmlTxt *h) { htNewline(h); htFlush(h); return !h->failed; }

// ---------- OPF / พาธ ----------
static bool xIsWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
// ค่าของ attribute `name` ในแท็กช่วง [tag, end)
static bool xAttr(const char *tag, const char *end, const char *name, char *out, int cap) {
  size_t nl = strlen(name);
  const char *p = tag;
  while (p < end) {
    const char *q = (const char *)memchr(p, name[0], end - p);
    if (!q) return false;
    if ((size_t)(end - q) > nl && memcmp(q, name, nl) == 0 && xIsWs(q[-1])) {
      const char *r = q + nl;
      while (r < end && xIsWs(*r)) r++;
      if (r < end && *r == '=') {
        r++;
        while (r < end && xIsWs(*r)) r++;
        if (r < end && (*r == '"' || *r == '\'')) {
          char qc = *r++;
          const char *v = r;
          while (r < end && *r != qc) r++;
          if (r >= end) return false;
          int k = (int)(r - v);
          if (k >= cap) k = cap - 1;
          memcpy(out, v, k); out[k] = 0;
          return true;
        }
      }
    }
    p = q + 1;
  }
  return false;
}
// หาแท็กเปิด <name ...> ถัดไปจาก from; คืนตำแหน่ง '<' และ '>' 
static const char *xTag(const char *from, const char *name, const char **tagEnd) {
  size_t nl = strlen(name);
  const char *p = from;
  while ((p = strchr(p, '<')) != nullptr) {
    if (strncmp(p + 1, name, nl) == 0) {
      char c = p[1 + nl];
      if (xIsWs(c) || c == '>' || c == '/') {
        const char *e = strchr(p, '>');
        if (!e) return nullptr;
        *tagEnd = e;
        return p;
      }
    }
    p++;
  }
  return nullptr;
}
static int xHex(char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); }
// href -> พาธใน zip: ถอด %XX, ตัด #fragment, ต่อกับโฟลเดอร์ของ OPF, จัดการ ../ และ ./
static void xResolve(const char *baseDir, const char *href, char *out, int cap) {
  char dec[256]; int n = 0;
  for (const char *p = href; *p && *p != '#' && *p != '?' && n < (int)sizeof(dec) - 1; p++) {
    if (*p == '%' && xHex(p[1]) >= 0 && xHex(p[2]) >= 0) { dec[n++] = (char)(xHex(p[1]) * 16 + xHex(p[2])); p += 2; }
    else dec[n++] = *p;
  }
  dec[n] = 0;
  char tmp[400];
  if (dec[0] == '/') snprintf(tmp, sizeof(tmp), "%s", dec + 1);
  else snprintf(tmp, sizeof(tmp), "%s%s", baseDir, dec);
  // ลบ . และ ..
  char *segs[48]; int ns = 0;
  char *p = tmp;
  while (*p) {
    char *s = p;
    while (*p && *p != '/') p++;
    bool more = (*p == '/');
    if (*p) *p++ = 0;
    if (strcmp(s, ".") == 0 || (s[0] == 0 && more)) continue;
    if (strcmp(s, "..") == 0) { if (ns > 0) ns--; continue; }
    if (ns < 48) segs[ns++] = s;
  }
  int o = 0;
  for (int i = 0; i < ns; i++) {
    int l = (int)strlen(segs[i]);
    if (o + l + 2 >= cap) break;
    if (i) out[o++] = '/';
    memcpy(out + o, segs[i], l); o += l;
  }
  out[o] = 0;
}
// รายการถัดไปใน spine: cur เริ่มที่ nullptr; คืน true พร้อม idref
static bool opfNextSpine(const char *opf, const char **cur, char *idref, int cap) {
  const char *e;
  if (!*cur) {
    const char *sp = xTag(opf, "spine", &e);
    if (!sp) return false;
    *cur = e;
  }
  const char *end = strstr(*cur, "</spine");
  for (;;) {
    const char *t = xTag(*cur, "itemref", &e);
    if (!t || (end && t > end)) return false;
    *cur = e;
    if (xAttr(t, e, "idref", idref, cap)) return true;
  }
}
// หา <item id=idref ...> ในรายการ manifest
static bool opfItem(const char *opf, const char *idref, char *href, int hcap, char *mtype, int mcap) {
  const char *e, *from = opf;
  for (;;) {
    const char *t = xTag(from, "item", &e);
    if (!t) return false;
    from = e;
    char id[96];
    if (xAttr(t, e, "id", id, sizeof(id)) && strcmp(id, idref) == 0) {
      if (!xAttr(t, e, "href", href, hcap)) return false;
      if (!xAttr(t, e, "media-type", mtype, mcap)) mtype[0] = 0;
      return true;
    }
  }
}
