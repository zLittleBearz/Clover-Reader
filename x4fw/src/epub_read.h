#pragma once
// epub_read.h : เปิดอ่าน EPUB — แปลงเป็นข้อความแคชบน SD ครั้งแรก แล้วอ่านทีละหน้า
// ต้อง include หลัง epub.h, epub_text.h, thai.h และฟังก์ชัน header/footer/msg/present/waitBtn
//   แคช:  /.clover/<crc>_<size>.txt  (ข้อความ UTF-8, ย่อหน้าคั่นด้วย \n)
//   ตำแหน่งที่อ่านค้าง: /.clover/<crc>_<size>.pos  (เลขไบต์ฐานสิบ)
// ข้อกำหนดบัส: อ่าน/เขียน SD ให้จบก่อนค่อยเรียก present()

#define EP_DIR "/.clover"
#define RD_FI 0            // ขนาดฟอนต์ที่ใช้อ่าน (0 = ใหญ่, 1 = เล็ก)
#define RD_X 24            // ขอบซ้าย
#define RD_W 432           // ความกว้างบรรทัด
#define RD_INDENT 30       // ย่อหน้าบรรทัดแรก
#define RD_Y0 60           // เส้นฐานบรรทัดแรก
#define RD_Y1 740          // เส้นฐานบรรทัดสุดท้ายไม่เกินนี้
#define RD_MAXL 40
#define RD_CHUNK 192       // ไบต์ที่อ่านต่อหนึ่งบรรทัดเพื่อหาจุดตัด

// ---------- สร้างแคชข้อความ ----------
static bool epFileSink(const uint8_t *p, size_t n, void *c) { return ((File *)c)->write(p, n) == n; }
struct EpMem { char *p; size_t n, cap; };
static bool epMemSink(const uint8_t *p, size_t n, void *c) {
  EpMem *m = (EpMem *)c;
  size_t k = m->cap > m->n ? m->cap - m->n : 0;
  if (k > n) k = n;
  memcpy(m->p + m->n, p, k); m->n += k; return true;   // เกินความจุแล้วทิ้งส่วนท้าย (ยังนับ CRC ครบ)
}
static HtmlTxt epHt;

static void epKey(const char *full, uint32_t size, char *out, int cap) {
  uint32_t c = ~zCrc(0xFFFFFFFF, (const uint8_t *)full, strlen(full));
  snprintf(out, cap, "%s/%08x_%x", EP_DIR, (unsigned)c, (unsigned)size);
}

// คืน 0 สำเร็จ; ลบ = ผิดพลาด (-1 เปิดไฟล์, -2 ไม่ใช่ zip, -3 ไม่พบ container/OPF, -4 หน่วยความจำ, -5 ไม่มีข้อความ, -6 เปิดไฟล์ปลายทางไม่ได้)
static int epConvert(const char *full, const char *outPath, void (*progress)(int, int)) {
  File zf = SD.open(full, FILE_READ);
  if (!zf) return -1;
  uint32_t cd, n;
  if (!zipOpen(zf, cd, n)) { zf.close(); return -2; }
  static char xml[2048];
  char rootPath[160] = "";
  ZEnt e;
  EpMem xm = {xml, 0, sizeof(xml) - 1};
  if (!zipFind(zf, cd, n, "META-INF/container.xml", e) || zipExtract(zf, e, epMemSink, &xm) != 0) { zf.close(); return -3; }
  xml[xm.n] = 0;
  const char *rf, *re;
  if (!(rf = xTag(xml, "rootfile", &re)) || !xAttr(rf, re, "full-path", rootPath, sizeof(rootPath))) { zf.close(); return -3; }
  ZEnt eo;
  if (!zipFind(zf, cd, n, rootPath, eo) || eo.usz > 120000) { zf.close(); return -3; }
  char *opf = (char *)malloc(eo.usz + 1);
  if (!opf) { zf.close(); return -4; }
  EpMem om = {opf, 0, eo.usz};
  if (zipExtract(zf, eo, epMemSink, &om) != 0) { free(opf); zf.close(); return -3; }
  opf[om.n] = 0;
  char base[160];
  snprintf(base, sizeof(base), "%s", rootPath);
  char *sl = strrchr(base, '/');
  if (sl) sl[1] = 0; else base[0] = 0;
  // นับบทใน spine เพื่อแสดงความคืบหน้า
  int total = 0;
  { const char *cur = nullptr; char id[96]; while (opfNextSpine(opf, &cur, id, sizeof(id))) total++; }
  SD.mkdir(EP_DIR);
  SD.remove(outPath);
  File of = SD.open(outPath, FILE_WRITE);
  if (!of) { free(opf); zf.close(); return -6; }
  htInit(&epHt, epFileSink, &of);
  const char *cur = nullptr;
  char id[96], href[200], mt[80], path[300];
  int idx = 0;
  while (opfNextSpine(opf, &cur, id, sizeof(id))) {
    idx++;
    if (!opfItem(opf, id, href, sizeof(href), mt, sizeof(mt))) continue;
    if (!strstr(mt, "html")) continue;
    xResolve(base, href, path, sizeof(path));
    if (progress) progress(idx, total);
    ZEnt ce;
    if (!zipFind(zf, cd, n, path, ce)) continue;
    htBegin(&epHt);
    zipExtract(zf, ce, htSink, &epHt);
    if (epHt.failed) break;
  }
  bool okW = htFinish(&epHt);
  uint32_t bytes = epHt.written;
  of.close(); free(opf); zf.close();
  if (!okW) { SD.remove(outPath); return -6; }
  if (bytes < 8) { SD.remove(outPath); return -5; }
  return 0;
}

// ---------- จัดบรรทัด ----------
struct RdLine { uint32_t off; int x; char t[RD_CHUNK + 8]; };
static RdLine rdL[RD_MAXL];
static int rdN = 0, rdPerPage = 1, rdPitch = 40;
static File rdF;
static uint32_t rdSize = 0, rdStart = 0, rdEnd = 0;
static ThCl rdCl[TH_MAXC];
static bool rdWb[TH_MAXC];

// จัด 1 บรรทัดที่เริ่มตาม off; คืน false ถ้าจบไฟล์. next = ตำแหน่งบรรทัดถัดไป
static bool rdLine(uint32_t off, RdLine *ln, uint32_t &next) {
  for (;;) {
    if (off >= rdSize) return false;
    uint32_t from = off ? off - 1 : 0;
    uint8_t buf[RD_CHUNK + 2];
    if (!rdF.seek(from)) return false;
    int got = rdF.read(buf, sizeof(buf) - 1);
    if (got <= 0) return false;
    int skip = off ? 1 : 0;                       // ไบต์แรกคือไบต์ก่อนหน้า
    bool paraStart = (off == 0) || buf[0] == '\n';
    int p = skip, avail = got;
    while (p < avail && buf[p] == ' ') p++;       // ข้ามช่องว่างหน้าบรรทัด
    if (p >= avail) { off += (p - skip); if (p - skip == 0) return false; continue; }
    if (buf[p] == '\n') { off += (p - skip) + 1; continue; }   // บรรทัดว่าง
    int tl = 0;
    while (p + tl < avail && buf[p + tl] != '\n') tl++;
    bool hasNL = (p + tl < avail);
    if (!hasNL && p + tl >= avail && got == (int)sizeof(buf) - 1) {   // อ่านเต็มก้อน อาจตัดกลางตัวอักษร UTF-8
      int k = p + tl - 1;
      while (k > p && (buf[k] & 0xC0) == 0x80) k--;
      int need = buf[k] >= 0xF0 ? 4 : (buf[k] >= 0xE0 ? 3 : (buf[k] >= 0xC0 ? 2 : 1));
      if (k + need > p + tl) tl = k - p;
      if (tl == 0) tl = 1;
    }
    char tmp[RD_CHUNK + 4];
    memcpy(tmp, buf + p, tl); tmp[tl] = 0;
    int ind = paraStart && (p - skip) == 0 ? RD_INDENT : 0;
    int maxw = RD_W - ind;
    int nc = thSegment(tmp, RD_FI, rdCl, TH_MAXC);
    thWordBreaks(tmp, rdCl, nc, rdWb);
    int w = 0, i = 0, brk = -1;
    for (; i < nc; i++) {
      if (w + rdCl[i].adv > maxw && i > 0) break;
      w += rdCl[i].adv;
      if (i + 1 < nc && !rdCl[i + 1].noBreakBefore && rdWb[i + 1]) brk = i + 1;   // ตัดได้เฉพาะระหว่างคำ
      if (rdCl[i].space && i + 1 < nc) brk = i + 1;
      if (i + 1 < nc && (tmp[rdCl[i].start] == '_' || tmp[rdCl[i].start] == '-' || tmp[rdCl[i].start] == '/')) brk = i + 1;
    }
    int cut;                                       // ไบต์ของข้อความที่อยู่ในบรรทัดนี้
    if (i >= nc) cut = tl;
    else { int e = brk > 0 ? brk : i; if (e < 1) e = 1; cut = rdCl[e].start; }
    int used = (i >= nc && hasNL) ? tl + 1 : cut;  // ไบต์ที่ใช้ไปจริง (รวม \n ถ้าจบย่อหน้า)
    int lt = cut;
    while (lt > 0 && tmp[lt - 1] == ' ') lt--;
    if (lt >= (int)sizeof(ln->t)) lt = sizeof(ln->t) - 1;
    memcpy(ln->t, tmp, lt); ln->t[lt] = 0;
    ln->x = RD_X + ind;
    ln->off = off;
    next = off + (p - skip) + used;
    if (next <= off) next = off + 1;
    return true;
  }
}
static void rdFill(uint32_t start) {
  rdN = 0;
  uint32_t o = start, nx;
  while (rdN < rdPerPage && rdLine(o, &rdL[rdN], nx)) { rdL[rdN].off = o; o = nx; rdN++; }
  rdStart = start; rdEnd = o;
}
// หาจุดเริ่มหน้าก่อนหน้า: ย้อน rdPerPage บรรทัดจาก cur
static uint32_t rdBack(uint32_t cur) {
  uint32_t starts[RD_MAXL];            // เก็บจากหลังไปหน้า
  int have = 0;
  uint32_t pos = cur;
  static RdLine tmpL;
  while (have < rdPerPage && pos > 0) {
    // หาต้นย่อหน้าที่มีไบต์ pos-1
    uint32_t p0 = 0;
    if (pos >= 2) {                      // ย้อนอ่านทีละก้อน 256 ไบต์หา \n
      uint32_t hi = pos - 1;             // ตรวจไบต์ในช่วง [lo, hi)
      while (hi > 0) {
        uint32_t lo = hi > 256 ? hi - 256 : 0;
        uint8_t bb[256];
        if (!rdF.seek(lo) || rdF.read(bb, hi - lo) != (int)(hi - lo)) break;
        int k = (int)(hi - lo) - 1;
        for (; k >= 0; k--) if (bb[k] == '\n') break;
        if (k >= 0) { p0 = lo + k + 1; break; }
        hi = lo;
      }
    }
    uint32_t ring[RD_MAXL]; int rc = 0, rn = 0;
    uint32_t o = p0, nx;
    while (o < pos && rdLine(o, &tmpL, nx)) {
      if (o >= pos) break;
      ring[rc % RD_MAXL] = o; rc++; if (rn < RD_MAXL) rn++;
      o = nx;
    }
    for (int k = 0; k < rn && have < rdPerPage; k++) starts[have++] = ring[(rc - 1 - k + RD_MAXL * 4) % RD_MAXL];
    pos = p0;
  }
  if (have == 0) return 0;
  return starts[have - 1];
}

// ---------- หน้าจอ ----------
static void rdDraw() {
  cv->fillScreen(0);
  cv->setTextColor(1);
  for (int i = 0; i < rdN; i++) thDraw(rdL[i].x, RD_Y0 + i * rdPitch, rdL[i].t, RD_FI);
  char pg[24];
  int pct = rdSize ? (int)(((uint64_t)rdStart * 100) / rdSize) : 0;
  if (rdEnd >= rdSize) pct = 100;
  snprintf(pg, sizeof(pg), "%d%%", pct);
  thDraw(480 - 16 - thWidth(pg, 1), 784, pg, 1);
}
static void epProgressUI(int i, int n) {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (i > 1 && now - last < 1500) return;
  last = now;
  char ln[48];
  snprintf(ln, sizeof(ln), "บทที่ %d / %d", i, n);
  header("กำลังเตรียมหนังสือ");
  thWrapDraw(16, 170, ln, 0, 448, 1);
  footer("ทำครั้งแรกครั้งเดียว โปรดรอสักครู่");
  present();
}
static void epSavePos(const char *posPath, uint32_t off) {
  SD.remove(posPath);
  File f = SD.open(posPath, FILE_WRITE);
  if (f) { char b[16]; int l = snprintf(b, sizeof(b), "%u", (unsigned)off); f.write((const uint8_t *)b, l); f.close(); }
}

static void epubReadPath(const char *full, const char *shortName) {
  uint32_t zsz = 0;
  { File t = SD.open(full, FILE_READ); if (!t) { msg("EPUB", "เปิดไฟล์ไม่ได้"); return; } zsz = t.size(); t.close(); }
  char key[96], txt[112], pos[112];
  epKey(full, zsz, key, sizeof(key));
  snprintf(txt, sizeof(txt), "%s.txt", key);
  snprintf(pos, sizeof(pos), "%s.pos", key);
  DBG("rd-cache");
  if (!SD.exists(txt)) {
    char tmp[112];
    snprintf(tmp, sizeof(tmp), "%s.tmp", key);
    epProgressUI(1, 1);
    int r = epConvert(full, tmp, epProgressUI);
    if (r == 0) { SD.remove(txt); SD.rename(tmp, txt); }
    else {
      char m[64];
      snprintf(m, sizeof(m), "แปลงหนังสือไม่สำเร็จ (รหัส %d)", r);
      msg("EPUB", m, shortName);
      return;
    }
  }
  DBG("rd-open");
  rdF = SD.open(txt, FILE_READ);
  if (!rdF) { msg("EPUB", "เปิดแคชไม่ได้"); return; }
  rdSize = rdF.size();
  rdPitch = thPitch(RD_FI) + 8;
  rdPerPage = (RD_Y1 - RD_Y0) / rdPitch + 1;
  if (rdPerPage > RD_MAXL) rdPerPage = RD_MAXL;
  if (rdPerPage < 1) rdPerPage = 1;
  uint32_t start = 0;
  { File pf = SD.open(pos, FILE_READ);
    if (pf) { char b[16] = ""; int l = pf.read((uint8_t *)b, sizeof(b) - 1); if (l > 0) { b[l] = 0; start = (uint32_t)strtoul(b, nullptr, 10); } pf.close(); } }
  if (start >= rdSize) start = 0;
  rdFill(start);
  DBG("rd-draw");
  rdDraw(); present();
  int turns = 0;
  for (;;) {
    int b = waitBtn();
    bool redraw = false;
    if (b == RIGHT || b == DOWN) {
      if (rdEnd < rdSize) { rdFill(rdEnd); redraw = true; }
    } else if (b == LEFT || b == UP) {
      if (rdStart > 0) { uint32_t s = rdBack(rdStart); rdFill(s); redraw = true; }
    } else if (b == BACK) {
      break;
    }
    if (redraw) {
      if (++turns % 5 == 0) epSavePos(pos, rdStart);
      rdDraw(); present();
    }
  }
  epSavePos(pos, rdStart);
  rdF.close();
}
