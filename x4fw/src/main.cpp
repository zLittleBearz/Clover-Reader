#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(16 * 1024);   // เพิ่มสแตกของ loop (กันสแตกล้น)
#endif
static RTC_NOINIT_ATTR char dbgStep[24];       // เก็บ "ขั้นตอนล่าสุด" ไว้ดูหลังรีบูตผิดปกติ
static RTC_NOINIT_ATTR uint32_t dbgMagic;
static char rstInfo[120] = "";
#define DBG(s) do { strlcpy(dbgStep, (s), sizeof(dbgStep)); dbgMagic = 0xC10E; } while (0)
#define REFRESH_HOLD_MS 1200
#include <Adafruit_GFX.h>
#include "icons.h"
#include "epub.h"
#include "epub_text.h"

#define VERSION "v0.8b " __DATE__ " " __TIME__
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
static void pollDuring();   // นิยามไว้ข้างล่าง
static void waitBusy() {
  uint32_t t = millis();
  while (digitalRead(EPD_BUSY) == HIGH && millis() - t < 15000) { pollDuring(); delay(5); }
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
static int fullEvery = 0;   // 0 = ปิดรีเฟรชเต็มจออัตโนมัติ (กด OK ค้างเพื่อรีเฟรชเอง)
static bool epdOn = false;   // จอตื่นอยู่ (ยังไม่ deep sleep)
static uint32_t lastPresentMs = 0, dbgInit = 0, dbgRam = 0, dbgRef = 0;
static void epdPark() {
  if (!epdOn) return;
  cmd(0x22); dat(0x03); cmd(0x20); waitBusy();   // ปิด analog/clock
  cmd(0x10); dat(0x01);                          // deep sleep
  epdOn = false;
}
static void present(bool forceFull = false) {
  bool full = forceFull || firstDraw || (fullEvery > 0 && partialCount >= fullEvery);
  const uint8_t *b = cv->getBuffer();
  uint32_t t0 = millis();
  if (full || !epdOn) { epdInit(); epdOn = true; }
  uint32_t t1 = millis();
  if (full) {
    sendRam(0x24, b);
    sendRam(0x26, b);
  } else {
    sendRam(0x24, b);
    sendRam(0x26, prevBuf);
  }
  uint32_t t2 = millis();
  if (full) {
    cmd(0x22); dat(0xF7);
    cmd(0x20); waitBusy();
    partialCount = 0;
    firstDraw = false;
  } else {
    cmd(0x22); dat(PARTIAL_CTRL2);
    cmd(0x20); waitBusy();
    partialCount++;
  }
  memcpy(prevBuf, b, sizeof(prevBuf));
  dbgInit = t1 - t0; dbgRam = t2 - t1; dbgRef = millis() - t2;
  lastPresentMs = millis();
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
// ---------- Deep sleep (กด Power ค้าง) ----------
#define SLEEP_HOLD_MS 1500
static void armSleep() {
  gpio_pullup_en((gpio_num_t)BTN_PWR);
  gpio_hold_en((gpio_num_t)EPD_CS);
  gpio_hold_en((gpio_num_t)SD_CS);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_PWR, ESP_GPIO_WAKEUP_GPIO_LOW);
}
static void goSleep() {
  cv->fillScreen(0);
  cv->setTextColor(1);
  thDraw(24, 76, "Clover Reader", 0);
  cv->fillRect(0, 100, 480, 4, 1);
  thWrapDraw(24, 300, "ปิดเครื่องแล้ว", 0, 432, 2);
  thWrapDraw(24, 420, "กดปุ่ม Power ค้างเพื่อเปิดเครื่อง", 1, 432, 2);
  present(true);
  epdPark();
  while (digitalRead(BTN_PWR) == LOW) delay(10);
  delay(100);
  armSleep();
  esp_deep_sleep_start();
}
// ---------- ปุ่ม: อ่านเป็น "สถานะ" ที่นิ่งพอ ไม่ใช้คิว ----------
#define CATCH_DURING_REFRESH 1   // 1 = จับการกดขณะจอรีเฟรช (1 ครั้ง) ถ้าปุ่มเพี้ยนให้ตั้งเป็น 0
#define HOLD_FIRST_MS 600
#define HOLD_REP_MS 120
#define BTN_UNSTABLE -2
static int heldBtn = NONE, pendBtn = NONE;
static bool kbBackRep = false;   // true = กด BACK ค้างแล้วลบต่อเนื่อง (ใช้ในคีย์บอร์ด)
static uint32_t heldSince = 0, lastRep = 0;
static int pdCand = NONE, pdCnt = 0;
static bool isNav(int b) { return b == UP || b == DOWN || b == LEFT || b == RIGHT; }
static int navDir(int b) { return (b == UP || b == LEFT) ? -1 : (b == DOWN || b == RIGHT) ? 1 : 0; }
static int navSteps(int b) { return navDir(b); }
static void qclear() { pendBtn = NONE; }
static void idleTick() { if (epdOn && millis() - lastPresentMs > 2000) epdPark(); }
static int stableBtn() {   // ต้องอ่านได้ค่าเดิม 4 ครั้งติด
  int c = readBtn();
  for (int i = 1; i < 4; i++) { delay(4); if (readBtn() != c) return BTN_UNSTABLE; }
  return c;
}
static void pollDuring() {   // เรียกระหว่างรอจอรีเฟรช
#if CATCH_DURING_REFRESH
  int r = readBtn();
  if (r == pdCand) { if (pdCnt < 50) pdCnt++; } else { pdCand = r; pdCnt = 1; }
  if (pdCnt == 8) {   // นิ่งประมาณ 40 ms
    if (pdCand == NONE) heldBtn = NONE;   // เห็นการปล่อยปุ่ม
    else if (pdCand != heldBtn) { pendBtn = pdCand; heldBtn = pdCand; }
  }
#endif
}
static int waitBtn() {
  while (true) {
    int b;
    if (pendBtn != NONE) {
      b = pendBtn; pendBtn = NONE;
      heldBtn = b; heldSince = lastRep = millis();
    } else {
      int c = stableBtn();
      if (c == BTN_UNSTABLE) { delay(2); continue; }
      uint32_t now = millis();
      if (c == NONE) { heldBtn = NONE; idleTick(); delay(8); continue; }
      if (c != heldBtn) { heldBtn = c; heldSince = lastRep = now; b = c; }
      else if ((isNav(c) || (kbBackRep && c == BACK)) && now - heldSince > HOLD_FIRST_MS && now - lastRep > HOLD_REP_MS) { lastRep = now; b = c; }
      else { delay(8); continue; }
    }
    if (b == POWER) {
      uint32_t t0 = millis();
      while (readBtn() == POWER) {
        if (millis() - t0 >= SLEEP_HOLD_MS) goSleep();
        delay(10);
      }
    }
    if (b == CONFIRM) {
      uint32_t t0 = millis();
      bool did = false;
      while (readBtn() == CONFIRM) {
        if (!did && millis() - t0 >= REFRESH_HOLD_MS) { present(true); did = true; }
        delay(10);
      }
      if (did) { heldBtn = NONE; continue; }   // กด OK ค้าง = รีเฟรชจอเอง (ไม่ส่ง OK)
    }
    return b;
  }
}

// ---------- UI helpers ----------
// ---------- แบตเตอรี่ (GPIO0 ผ่านตัวหารแรงดัน 1/2) ----------
static int batMv = 0, batPct = -1;
static uint32_t batAt = 0;
static void batUpdate() {
  if (batAt && millis() - batAt < 60000) return;      // อ่านใหม่ไม่เกินนาทีละครั้ง
  batAt = millis() ? millis() : 1;
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(0);
  int mv = (int)(sum / 8) * 2;
  batMv = mv;
  if (mv < 2600 || mv > 4600) { batPct = -1; return; }   // ค่าผิดปกติ: ไม่แสดง
  double v = mv / 1000.0;
  double y = -144.9390 * v * v * v + 1655.8629 * v * v - 6158.8520 * v + 7501.3202;   // เส้นโค้งลิเธียมไอออน
  batPct = y < 0 ? 0 : (y > 100 ? 100 : (int)(y + 0.5));
}
static void drawBattery(int baseline) {
  batUpdate();
  if (batPct < 0) return;
  char b[8];
  snprintf(b, sizeof(b), "%d%%", batPct);
  cv->setTextColor(1);
  thDraw(480 - 16 - thWidth(b, 1), baseline, b, 1);
}
static void header(const char *t) {
  cv->fillScreen(0);
  cv->setTextColor(1);
  thWrapDraw(16, 64, t, 0, 380, 1);
  drawBattery(64);
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

#include "kbd.h"

// เมนูรายการ: ตัวใหญ่ ขึ้นได้ 2 บรรทัด ไฮไลท์เทา คืนค่า index หรือ -1 (BACK)
static int menu(const char *title, String *items, int n, int ts, const char *foot, int sel0 = 0) {
  const int ROWS = 6, RH = 100, Y0 = 110;
  (void)ts;
  qclear();
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
    for (;;) {
      int b = waitBtn();
      if (isNav(b)) {
        int ns = sel + navSteps(b);
        ns = ns < 0 ? 0 : (ns > n - 1 ? n - 1 : ns);
        if (ns == sel) continue;   // อยู่ปลายรายการแล้ว ไม่ต้องรีเฟรช
        sel = ns; break;
      }
      if (b == CONFIRM || b == POWER) return sel;
      if (b == BACK) return -1;
    }
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
// ---------- ทดสอบอ่าน EPUB (zip + inflate) v0.7a ----------
struct ZBuf { char *p; size_t cap, len; };
static bool zBufSink(const uint8_t *d, size_t n, void *c) {
  ZBuf *b = (ZBuf *)c;
  size_t room = b->cap - 1 - b->len, k = n < room ? n : room;
  memcpy(b->p + b->len, d, k); b->len += k; b->p[b->len] = 0;
  return true;
}
static char epubXml[2048];
static void epubProbe(const String &dir, const String &name) {
  String full = (dir == "/" ? String("/") : dir + "/") + name;
  char rootPath[128] = "", title[160] = "";
  int r1 = -99, r2 = -99;
  uint32_t n = 0, cd = 0, ms = 0, usz = 0;
  DBG("ep-open");
  File f = SD.open(full.c_str(), FILE_READ);
  if (!f) { msg("EPUB", "เปิดไฟล์ไม่ได้"); return; }
  bool zipOk = zipOpen(f, cd, n);
  ZEnt e;
  if (zipOk) {
    DBG("ep-cont");
    ZBuf zb = {epubXml, sizeof(epubXml), 0};
    epubXml[0] = 0;
    if (zipFind(f, cd, n, "META-INF/container.xml", e)) r1 = zipExtract(f, e, zBufSink, &zb);
    char *q = strstr(epubXml, "full-path=\"");
    if (q) {
      q += 11;
      char *qe = strchr(q, '"');
      if (qe) {
        size_t k = qe - q;
        if (k >= sizeof(rootPath)) k = sizeof(rootPath) - 1;
        memcpy(rootPath, q, k); rootPath[k] = 0;
      }
    }
    if (rootPath[0]) {
      DBG("ep-opf");
      ZBuf ob = {epubXml, sizeof(epubXml), 0};
      epubXml[0] = 0;
      uint32_t t0 = millis();
      ZEnt eo;
      if (zipFind(f, cd, n, rootPath, eo)) { r2 = zipExtract(f, eo, zBufSink, &ob); usz = eo.usz; }
      ms = millis() - t0;
      char *t = strstr(epubXml, "<dc:title");
      if (t && (t = strchr(t, '>'))) {
        t++;
        char *te = strchr(t, '<');
        size_t k = te ? (size_t)(te - t) : strlen(t);
        if (k >= sizeof(title)) k = sizeof(title) - 1;
        memcpy(title, t, k); title[k] = 0;
      }
    }
  }
  f.close();   // อ่าน SD ให้จบก่อนแล้วค่อยวาดจอ
  DBG("ep-draw");
  header("EPUB");
  cv->setTextColor(1);
  int y = 140;
  char ln[200];
  thWrapDraw(16, y, name.c_str(), 1, 448, 2); y += 2 * thPitch(1) + 14;
  if (!zipOk) {
    thDraw(16, y, "ไม่ใช่ไฟล์ zip/epub", 0);
  } else {
    snprintf(ln, sizeof(ln), "รายการใน zip: %u", (unsigned)n);
    thDraw(16, y, ln, 1); y += thPitch(1) + 14;
    if (r1 == 0) snprintf(ln, sizeof(ln), "container.xml: ผ่าน (CRC ตรง)");
    else snprintf(ln, sizeof(ln), "container.xml: ล้มเหลว รหัส %d", r1);
    thDraw(16, y, ln, 1); y += thPitch(1) + 14;
    if (rootPath[0]) { thWrapDraw(16, y, rootPath, 1, 448, 2); y += 2 * thPitch(1) + 14; }
    if (r2 == 0) snprintf(ln, sizeof(ln), "OPF: %u ไบต์ %u ms ผ่าน (CRC ตรง)", (unsigned)usz, (unsigned)ms);
    else snprintf(ln, sizeof(ln), "OPF: ล้มเหลว รหัส %d", r2);
    thDraw(16, y, ln, 1); y += thPitch(1) + 14;
    if (title[0]) {
      thDraw(16, y, "ชื่อเรื่อง:", 1); y += thPitch(1) + 6;
      thWrapDraw(16, y, title, 0, 448, 3);
    }
  }
  footer("กดปุ่มใดก็ได้");
  present();
  waitBtn();
}
#include "thai_dict.h"
#include "thai_wb.h"
#include "epub_read.h"
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
      {
        String ln = libNames[s]; ln.toLowerCase();
        if (ln.endsWith(".epub")) {
          String fp = (cwd == "/" ? String("/") : cwd + "/") + libNames[s];
          epubReadPath(fp.c_str(), libNames[s].c_str());
        }
        else fileScreen(cwd, libNames[s]);
      }
    }
  }
}

// ---------- Home: แถบไอคอน 4 ตัวด้านล่าง ----------
#define N_ICONS 4
#define CELL_W 120
#define TILE 96
#define TILE_Y 668
static const uint8_t *const ICONS[N_ICONS] = {ICON_LIBRARY, ICON_APPS, ICON_UPDATE, ICON_SETTING};
static const char *const LABELS[N_ICONS] = {"ห้องสมุด", "เพิ่มเติม", "อัปเดต", "ตั้งค่า"};

static void drawHome(int sel) {
  cv->fillScreen(0);
  cv->setTextColor(1);
  thDraw(24, 76, "X4 Reader", 0);
  drawBattery(76);
  cv->fillRect(0, 100, 480, 4, 1);
  cv->setTextSize(1);
  cv->setCursor(24, 114);
  cv->print(VERSION);
  cv->print(" i"); cv->print((unsigned)dbgInit);
  cv->print(" r"); cv->print((unsigned)dbgRam);
  cv->print(" d"); cv->print((unsigned)dbgRef);
  if (rstInfo[0]) thWrapDraw(16, 160, rstInfo, 1, 448, 3);
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
  epdPark();
  String err;
  if (!flashFile(path, err)) { msg("ล้มเหลว", err); return; }
  SD.rename(path.c_str(), (path + ".done").c_str());
  header("เสร็จแล้ว");
  thDraw(16, 170, "กำลังรีสตาร์ต...", 0);
  present(true);
  epdPark();
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
    batAt = 0; batUpdate();
    cv->setCursor(16, 330); cv->print("BAT mV: "); cv->print(batMv);
    footer("POWER = ออก");
    present();
    while ((b = readBtn()) == NONE) { idleTick(); delay(10); }
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
#include "wifi_xfer.h"
static void appsMenu() {
  String items[4] = {"รับส่งไฟล์ WiFi", "ทดสอบคีย์บอร์ด", "Button test", "ทดสอบฟอนต์"};
  int s = menu("เพิ่มเติม", items, 4, 3, "OK=เปิด  BACK=กลับ");
  if (s == 0) wifiMenu();
  else if (s == 1) kbTest();
  else if (s == 2) buttonTest();
  else if (s == 3) fontTest();
}

static void openIcon(int i) {
  if (i == 0) library();
  else if (i == 1) appsMenu();
  else if (i == 2) updateMenu();
  else if (i == 3) settingsMenu();
}
static void home() {
  int sel = 0;
  while (true) {
    drawHome(sel);
    present();
    int b = waitBtn();
    if (isNav(b)) sel = ((sel + navSteps(b)) % N_ICONS + N_ICONS) % N_ICONS;
    else if (b == CONFIRM || b == POWER) { openIcon(sel); qclear(); }
  }
}

void setup() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)EPD_CS);
  gpio_hold_dis((gpio_num_t)SD_CS);
  {
    esp_reset_reason_t rr = esp_reset_reason();
    if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT)
      snprintf(rstInfo, sizeof(rstInfo), "รีบูตผิดปกติ (รหัส %d) ขั้นตอนล่าสุด: %s", (int)rr, dbgMagic == 0xC10E ? dbgStep : "?");
    dbgMagic = 0;
    Preferences q;
    q.begin("ui", false);
    fullEvery = q.getInt("fe", 0);
    q.end();
  }
  pinMode(EPD_CS, OUTPUT); digitalWrite(EPD_CS, HIGH);
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_RST, OUTPUT); digitalWrite(EPD_RST, HIGH);
  pinMode(EPD_BUSY, INPUT);
  pinMode(BTN_PWR, INPUT);
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
    uint32_t w0 = millis();
    while (digitalRead(BTN_PWR) == LOW && millis() - w0 < SLEEP_HOLD_MS) delay(10);
    if (digitalRead(BTN_PWR) == HIGH) {   // กดไม่นานพอ: หลับต่อโดยไม่แตะจอ
      armSleep();
      esp_deep_sleep_start();
    }
  }
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, -1);
  cv = new GFXcanvas1(PW, PH);
  cv->setRotation(ROTATION);
  uint32_t t = millis();
  while (readBtn() != NONE && millis() - t < 3000) delay(10);
}

void loop() {
  home();
}
