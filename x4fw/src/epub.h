#pragma once
// epub.h : อ่าน zip (EPUB) จาก SD + inflate ในตัว (ไม่พึ่งไลบรารี/ROM)
// ใช้กับ File ของ Arduino SD: seek(pos), read(buf,n), size()
// รอบนี้: เปิด zip, หาไฟล์ในไดเรกทอรีกลาง, แตกไฟล์ผ่าน sink แล้วตรวจ CRC32
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define Z_WIN 32768

struct ZEnt { uint32_t lho, csz, usz, crc; uint16_t method; };
typedef bool (*ZSink)(const uint8_t *p, size_t n, void *ctx);   // คืน false = หยุดแตก

static inline uint16_t zU16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t zU32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---------- CRC32 (ตารางครึ่งไบต์ ประหยัดแฟลช) ----------
static uint32_t zCrc(uint32_t c, const uint8_t *p, size_t n) {   // c = สถานะดิบ (เริ่มที่ 0xFFFFFFFF)
  static const uint32_t t[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
                                 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  while (n--) { c ^= *p++; c = (c >> 4) ^ t[c & 15]; c = (c >> 4) ^ t[c & 15]; }
  return c;
}

// ---------- zip: ไดเรกทอรีกลาง ----------
// หา End-of-central-directory; คืน offset ของ central directory กับจำนวนรายการ
static bool zipOpen(File &f, uint32_t &cdOff, uint32_t &nEnt) {
  uint32_t sz = f.size();
  if (sz < 22) return false;
  uint8_t b[1024];
  uint32_t maxBack = sz < 65557 ? sz : 65557, hi = sz;
  for (;;) {
    uint32_t lo = hi > 1024 ? hi - 1024 : 0, len = hi - lo;
    if (!f.seek(lo) || f.read(b, len) != (int)len) return false;
    for (int i = (int)len - 22; i >= 0; i--) {
      if (b[i] == 0x50 && b[i + 1] == 0x4B && b[i + 2] == 5 && b[i + 3] == 6) {
        nEnt = zU16(b + i + 10);
        cdOff = zU32(b + i + 16);
        return cdOff < sz;
      }
    }
    if (lo == 0 || sz - lo >= maxBack) return false;
    hi = lo + 21;   // ซ้อนทับเล็กน้อย กันลายเซ็นคร่อมขอบหน้าต่าง
  }
}
// อ่านรายการที่ pos; คืนตำแหน่งรายการถัดไป (0 = ผิดพลาด). name ถูกตัดถ้ายาวเกิน cap-1
static uint32_t zipNext(File &f, uint32_t pos, char *name, int cap, ZEnt &e) {
  uint8_t h[46];
  if (!f.seek(pos) || f.read(h, 46) != 46 || zU32(h) != 0x02014B50) return 0;
  int nl = zU16(h + 28), el = zU16(h + 30), cl = zU16(h + 32);
  int rd = nl < cap - 1 ? nl : cap - 1;
  if (rd > 0 && f.read((uint8_t *)name, rd) != rd) return 0;
  name[rd] = 0;
  e.method = zU16(h + 10); e.crc = zU32(h + 16); e.csz = zU32(h + 20); e.usz = zU32(h + 24); e.lho = zU32(h + 42);
  return pos + 46 + nl + el + cl;
}
static bool zipFind(File &f, uint32_t cdOff, uint32_t n, const char *want, ZEnt &e) {
  char nm[256];
  uint32_t pos = cdOff;
  for (uint32_t i = 0; i < n; i++) {
    pos = zipNext(f, pos, nm, sizeof(nm), e);
    if (!pos) return false;
    if (strcmp(nm, want) == 0) return true;
  }
  return false;
}
static bool zipDataPos(File &f, const ZEnt &e, uint32_t &pos) {
  uint8_t h[30];
  if (!f.seek(e.lho) || f.read(h, 30) != 30 || zU32(h) != 0x04034B50) return false;
  pos = e.lho + 30 + zU16(h + 26) + zU16(h + 28);
  return true;
}

// ---------- inflate (แบบ puff: เล็ก ช้าแต่พอ) สตรีมผ่านหน้าต่าง 32 KB ----------
struct ZInf {
  File *f; uint32_t left;
  uint8_t ib[512]; uint16_t ip, il;
  uint32_t bb; int bc;
  uint8_t *win; uint32_t wp, total, crc;
  ZSink sink; void *ctx;
  int err;
};
struct ZHuff { uint16_t cnt[16]; uint16_t sym[288]; };

static int zByte(ZInf *s) {
  if (s->ip >= s->il) {
    if (!s->left) return -1;
    uint16_t n = s->left < sizeof(s->ib) ? (uint16_t)s->left : (uint16_t)sizeof(s->ib);
    int r = s->f->read(s->ib, n);
    if (r <= 0) return -1;
    s->il = r; s->ip = 0; s->left -= r;
  }
  return s->ib[s->ip++];
}
static int zBits(ZInf *s, int need) {
  uint32_t v = s->bb;
  while (s->bc < need) {
    int c = zByte(s);
    if (c < 0) { s->err = 1; return 0; }
    v |= (uint32_t)c << s->bc; s->bc += 8;
  }
  s->bb = v >> need; s->bc -= need;
  return (int)(v & ((1u << need) - 1));
}
static bool zFlush(ZInf *s, uint32_t n) {
  if (!n) return true;
  s->crc = zCrc(s->crc, s->win, n);
  return s->sink(s->win, n, s->ctx);
}
static inline bool zPut(ZInf *s, uint8_t b) {
  s->win[s->wp++] = b; s->total++;
  if (s->wp == Z_WIN) { if (!zFlush(s, Z_WIN)) return false; s->wp = 0; }
  return true;
}
static int zDecode(ZInf *s, const ZHuff *h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= 15; len++) {
    code |= zBits(s, 1);
    if (s->err) return -1;
    int count = h->cnt[len];
    if (code - count < first) return h->sym[index + (code - first)];
    index += count; first += count; first <<= 1; code <<= 1;
  }
  return -1;
}
static int zBuild(ZHuff *h, const uint16_t *len, int n) {   // 0 = สมบูรณ์, >0 = ไม่ครบ, <0 = ผิด
  for (int i = 0; i < 16; i++) h->cnt[i] = 0;
  for (int i = 0; i < n; i++) h->cnt[len[i]]++;
  if (h->cnt[0] == n) return 0;
  int left = 1;
  for (int l = 1; l <= 15; l++) { left <<= 1; left -= h->cnt[l]; if (left < 0) return left; }
  uint16_t offs[16];
  offs[1] = 0;
  for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h->cnt[l];
  for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = i;
  return left;
}
static int zCodes(ZInf *s, const ZHuff *lc, const ZHuff *dc) {
  static const uint16_t lb[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint8_t le[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const uint16_t db[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t de[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  int sym;
  do {
    sym = zDecode(s, lc);
    if (sym < 0) return -1;
    if (sym < 256) {
      if (!zPut(s, (uint8_t)sym)) return -2;
    } else if (sym > 256) {
      sym -= 257;
      if (sym >= 29) return -3;
      int len = lb[sym] + zBits(s, le[sym]);
      int ds = zDecode(s, dc);
      if (ds < 0 || ds >= 30) return -4;
      uint32_t dist = db[ds] + zBits(s, de[ds]);
      if (s->err) return -1;
      if (dist > s->total) return -5;
      while (len--) if (!zPut(s, s->win[(s->wp - dist) & (Z_WIN - 1)])) return -2;
    }
  } while (sym != 256);
  return 0;
}
static int zStored(ZInf *s) {
  s->bb = 0; s->bc = 0;
  int a = zByte(s), b = zByte(s), c = zByte(s), d = zByte(s);
  if (a < 0 || b < 0 || c < 0 || d < 0) return -1;
  uint32_t len = a | (b << 8), nlen = c | (d << 8);
  if (len != (~nlen & 0xFFFF)) return -6;
  while (len--) {
    int x = zByte(s);
    if (x < 0) return -1;
    if (!zPut(s, (uint8_t)x)) return -2;
  }
  return 0;
}
static int zFixed(ZInf *s) {
  uint16_t lens[288];
  ZHuff lc, dc;
  int i = 0;
  for (; i < 144; i++) lens[i] = 8;
  for (; i < 256; i++) lens[i] = 9;
  for (; i < 280; i++) lens[i] = 7;
  for (; i < 288; i++) lens[i] = 8;
  zBuild(&lc, lens, 288);
  for (i = 0; i < 30; i++) lens[i] = 5;
  zBuild(&dc, lens, 30);
  return zCodes(s, &lc, &dc);
}
static int zDynamic(ZInf *s) {
  static const uint8_t ord[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  uint16_t lens[320];
  ZHuff lc, dc;
  int nlen = zBits(s, 5) + 257, ndist = zBits(s, 5) + 1, ncode = zBits(s, 4) + 4;
  if (s->err || nlen > 286 || ndist > 30) return -1;
  int i;
  for (i = 0; i < ncode; i++) lens[ord[i]] = zBits(s, 3);
  for (; i < 19; i++) lens[ord[i]] = 0;
  if (zBuild(&lc, lens, 19) != 0) return -7;
  i = 0;
  while (i < nlen + ndist) {
    int sym = zDecode(s, &lc);
    if (sym < 0) return -1;
    if (sym < 16) lens[i++] = sym;
    else {
      int len = 0, rep;
      if (sym == 16) { if (i == 0) return -8; len = lens[i - 1]; rep = 3 + zBits(s, 2); }
      else if (sym == 17) rep = 3 + zBits(s, 3);
      else rep = 11 + zBits(s, 7);
      if (i + rep > nlen + ndist) return -9;
      while (rep--) lens[i++] = len;
    }
  }
  if (s->err || lens[256] == 0) return -1;
  int e = zBuild(&lc, lens, nlen);
  if (e < 0 || (e > 0 && nlen != lc.cnt[0] + lc.cnt[1])) return -7;
  e = zBuild(&dc, lens + nlen, ndist);
  if (e < 0 || (e > 0 && ndist != dc.cnt[0] + dc.cnt[1])) return -7;
  return zCodes(s, &lc, &dc);
}
static int zInflate(ZInf *s) {
  int last, type, e;
  do {
    last = zBits(s, 1); type = zBits(s, 2);
    if (s->err) return -1;
    if (type == 0) e = zStored(s);
    else if (type == 1) e = zFixed(s);
    else if (type == 2) e = zDynamic(s);
    else e = -1;
    if (e) return e;
  } while (!last);
  return zFlush(s, s->wp) ? 0 : -2;
}

// แตกไฟล์ในรายการ e ส่งข้อมูลเข้า sink ทีละก้อน. คืน 0 = สำเร็จและ CRC ตรง
// ค่าลบ: -10..-12 เปิด/อ่านไม่ได้, -13 หน่วยความจำไม่พอ, -20 CRC ไม่ตรง, -21 ขนาดไม่ตรง, -22 วิธีบีบอัดที่ไม่รองรับ, อื่น ๆ = inflate ผิดพลาด
static int zipExtract(File &f, const ZEnt &e, ZSink sink, void *ctx) {
  uint32_t pos;
  if (!zipDataPos(f, e, pos)) return -10;
  if (!f.seek(pos)) return -11;
  if (e.method != 0 && e.method != 8) return -22;
  uint32_t crc = 0xFFFFFFFF, total = 0;
  int r = 0;
  if (e.method == 0) {
    uint8_t buf[512];
    uint32_t left = e.csz;
    while (left) {
      int n = left < sizeof(buf) ? (int)left : (int)sizeof(buf);
      if (f.read(buf, n) != n) return -12;
      crc = zCrc(crc, buf, n); total += n; left -= n;
      if (!sink(buf, n, ctx)) return -2;
    }
  } else {
    ZInf *s = (ZInf *)malloc(sizeof(ZInf));
    uint8_t *w = (uint8_t *)malloc(Z_WIN);
    if (!s || !w) { free(s); free(w); return -13; }
    s->f = &f; s->left = e.csz; s->ip = s->il = 0; s->bb = 0; s->bc = 0;
    s->win = w; s->wp = 0; s->total = 0; s->crc = 0xFFFFFFFF; s->sink = sink; s->ctx = ctx; s->err = 0;
    r = zInflate(s);
    crc = s->crc; total = s->total;
    free(w); free(s);
    if (r) return r;
  }
  if (total != e.usz) return -21;
  if ((~crc) != e.crc) return -20;
  return 0;
}
