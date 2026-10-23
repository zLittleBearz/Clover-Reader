#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include <Adafruit_GFX.h>
#include "icons.h"

#define VERSION "v0.3 " __DATE__ " " __TIME__
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
  cv->setTextSize(4);
  cv->setCursor(16, 28);
  cv->print(t);
  cv->fillRect(0, 90, 480, 4, 1);
}
static void footer(const char *t) {
  cv->setTextSize(2); cv->setTextColor(1);
  cv->setCursor(16, 765); cv->print(t);
}
static void msg(const char *title, const String &a, const String &b = "") {
  header(title);
  cv->setTextSize(3);
  cv->setCursor(16, 130); cv->print(a);
  cv->setCursor(16, 230); cv->print(b);
  footer("Press any button");
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

// ASCII เท่านั้น (ฟอนต์ไทยจะมาในขั้นถัดไป): ตัวอักษร UTF-8 หลายไบต์ -> '?' ตัวเดียว
static String asciiOnly(const String &in) {
  String out;
  for (int i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if (c < 0x80) out += (char)c;
    else if (c >= 0xC0) out += '?';
  }
  return out;
}
static void printWrapped(const String &s, int x, int y, int ts, int maxLines, int maxc) {
  int len = s.length();
  for (int k = 0; k < maxLines && k * maxc < len; k++) {
    int e = (k + 1) * maxc;
    if (e > len) e = len;
    String line = s.substring(k * maxc, e);
    if (k == maxLines - 1 && len > (k + 1) * maxc) line = line.substring(0, maxc - 2) + "..";
    cv->setCursor(x, y + k * (8 * ts + 4));
    cv->print(line);
  }
}
// เมนูรายการ: ตัวใหญ่ ขึ้นได้ 2 บรรทัด ไฮไลท์เทา คืนค่า index หรือ -1 (BACK)
static int menu(const char *title, String *items, int n, int ts, const char *foot, int sel0 = 0) {
  const int ROWS = 6, RH = 100, Y0 = 110;
  const int maxc = 440 / (6 * ts);
  int sel = sel0 < 0 ? 0 : (sel0 >= n ? n - 1 : sel0);
  while (true) {
    header(title);
    int first = sel - sel % ROWS;
    for (int i = 0; i < ROWS && first + i < n; i++) {
      int y = Y0 + i * RH;
      bool s = (first + i == sel);
      if (s) grayTile(8, y, 464, RH - 12, 12);
      cv->setTextColor(1);
      cv->setTextSize(ts);
      String t = asciiOnly(items[first + i]);
      int lines = (int)t.length() > maxc ? 2 : 1;
      int th = lines * 8 * ts + (lines - 1) * 4;
      printWrapped(t, 20, y + (RH - 12 - th) / 2, ts, 2, maxc);
    }
    footer(foot);
    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", sel + 1, n);
    cv->setTextSize(2);
    cv->setCursor(480 - 16 - (int)strlen(pg) * 12, 765);
    cv->print(pg);
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
  header("File");
  cv->setTextColor(1);
  cv->setTextSize(3);
  printWrapped(asciiOnly(name), 16, 120, 3, 4, 26);
  cv->setCursor(16, 300); cv->print(sb);
  cv->setCursor(16, 380); cv->print("Reader: coming soon");
  footer("Press any button");
  present();
  waitBtn();
}
static void library() {
  SD.end();
  if (!SD.begin(SD_CS, SPI, 20000000)) { msg("Library", "SD card error"); return; }
  String cwd = "/";
  int selStack[16];
  int depth = 0, sel = 0;
  while (true) {
    int n = listDir(cwd, LIB_MAX);
    if (n < 0) { msg("Library", "Cannot read", "this folder"); return; }
    if (n == 0) {
      msg("Library", "Empty folder");
      if (depth == 0) return;
      int i = cwd.lastIndexOf('/');
      cwd = i <= 0 ? String("/") : cwd.substring(0, i);
      sel = selStack[--depth];
      continue;
    }
    sortEntries(n);
    for (int i = 0; i < n; i++) libLabels[i] = libDirs[i] ? libNames[i] + "/" : libNames[i];
    String title = depth == 0 ? String("Library") : asciiOnly(leafOf(cwd));
    if (title.length() > 17) title = title.substring(0, 17);
    int s = menu(title.c_str(), libLabels, n, 4, "OK=open  BACK=up", sel);
    if (s < 0) {
      if (depth == 0) return;
      int i = cwd.lastIndexOf('/');
      cwd = i <= 0 ? String("/") : cwd.substring(0, i);
      sel = selStack[--depth];
    } else if (libDirs[s]) {
      if (depth >= 15) { msg("Library", "Too deep"); continue; }
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
static const char *const LABELS[N_ICONS] = {"Library", "Apps", "Update", "Setting"};

static void drawHome(int sel) {
  cv->fillScreen(0);
  cv->setTextColor(1);
  cv->setTextSize(4);
  cv->setCursor(24, 40);
  cv->print("X4 Reader");
  cv->fillRect(0, 100, 480, 4, 1);
  cv->setTextSize(1);
  cv->setCursor(24, 114);
  cv->print(VERSION);
  for (int i = 0; i < N_ICONS; i++) {
    int tx = i * CELL_W + (CELL_W - TILE) / 2;
    if (i == sel) grayTile(tx, TILE_Y, TILE, TILE, 14);
    cv->drawBitmap(tx + (TILE - 64) / 2, TILE_Y + (TILE - 64) / 2, ICONS[i], 64, 64, 1);
    cv->setTextSize(2);
    cv->setTextColor(1);
    int len = strlen(LABELS[i]);
    cv->setCursor(i * CELL_W + (CELL_W - len * 12) / 2, TILE_Y + TILE + 8);
    cv->print(LABELS[i]);
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
  if (!SD.begin(SD_CS, SPI, 20000000)) { msg("Update", "SD card error"); return; }
  static String files[16];
  int n = listBins(files, 16);
  if (n <= 0) { msg("Update", "No .bin on SD", "(put in root)"); return; }
  int s = menu("Pick .bin", files, n, 3, "OK=flash  BACK=cancel");
  if (s < 0) return;
  String path = "/" + files[s];
  header("Flashing...");
  cv->setTextSize(3);
  cv->setCursor(16, 130); cv->print("Do NOT power off");
  present();
  String err;
  if (!flashFile(path, err)) { msg("Failed", err); return; }
  SD.rename(path.c_str(), (path + ".done").c_str());
  header("Done");
  cv->setTextSize(3);
  cv->setCursor(16, 130); cv->print("Restarting...");
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
    footer("POWER = exit");
    present();
    while ((b = readBtn()) == NONE) delay(10);
    a1 = lastA1; a2 = lastA2;
    while (readBtn() != NONE) delay(10);
    delay(40);
    if (b == POWER) return;
  }
}
static void appsMenu() {
  String items[1] = {"Button test"};
  int s = menu("Apps", items, 1, 3, "OK=open  BACK=home");
  if (s == 0) buttonTest();
}

static void openIcon(int i) {
  if (i == 0) library();
  else if (i == 1) appsMenu();
  else if (i == 2) updateMenu();
  else if (i == 3) msg("Setting", "Coming soon");
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
