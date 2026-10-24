#pragma once
// รับส่งไฟล์ผ่าน WiFi (ฮอตสปอตของเครื่อง หรือ WiFi บ้าน) + หน้าเว็บจัดการไฟล์บน SD

static const char XF_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Clover Reader</title>
<style>
body{font:16px system-ui,sans-serif;margin:0;background:#f2f2f2;color:#111}
header{background:#2d6a4f;color:#fff;padding:12px 14px;font-size:18px}
main{padding:10px;max-width:720px;margin:auto}
a{color:#1b4332;text-decoration:none}
.bar{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0}
button{background:#2d6a4f;color:#fff;border:0;border-radius:8px;padding:10px 14px;font-size:15px}
button.s{background:#ddd;color:#111;padding:6px 10px}
button.r{background:#f4c7c3}
.row{display:flex;align-items:center;gap:8px;background:#fff;border-radius:8px;margin:6px 0;padding:8px 10px}
.row .n{flex:1;word-break:break-all}
small{color:#666}
#pb{height:6px;background:#ddd;border-radius:3px;margin:6px 0;display:none}
#pb i{display:block;height:100%;width:0;background:#2d6a4f;border-radius:3px}
</style></head><body>
<header>📚 Clover Reader – รับส่งไฟล์</header>
<main>
<div id="cr"></div>
<div class="bar"><button onclick="document.getElementById('f').click()">⬆ อัปโหลด</button>
<button onclick="mk()">📁 โฟลเดอร์ใหม่</button><button class="s" onclick="ls(cur)">↻</button></div>
<input id="f" type="file" multiple hidden onchange="up(this.files)">
<div id="st"></div><div id="pb"><i></i></div>
<div id="ls"></div>
</main>
<script>
var cur="/";
function $(i){return document.getElementById(i)}
function E(s){return encodeURIComponent(s)}
function sz(n){return n>=1048576?(n/1048576).toFixed(1)+" MB":Math.ceil(n/1024)+" KB"}
function st(t){$("st").textContent=t}
function jn(d,n){return d==="/"?"/"+n:d+"/"+n}
function crumbs(){
  var c=$("cr");c.textContent="";
  var a=document.createElement("a");a.textContent="🏠 SD";a.href="#";
  a.onclick=function(){ls("/");return false};c.appendChild(a);
  var acc="";
  cur.split("/").filter(Boolean).forEach(function(s){
    acc+="/"+s;var p=acc;
    c.appendChild(document.createTextNode(" / "));
    var b=document.createElement("a");b.textContent=s;b.href="#";
    b.onclick=function(){ls(p);return false};c.appendChild(b);
  });
}
function draw(items){
  items.sort(function(a,b){return a.d!==b.d?b.d-a.d:a.n.localeCompare(b.n,"th")});
  var L=$("ls");L.textContent="";
  if(!items.length){L.textContent="(ว่าง)";return}
  items.forEach(function(it){
    var full=jn(cur,it.n);
    var row=document.createElement("div");row.className="row";
    var n=document.createElement("div");n.className="n";
    var a=document.createElement("a");
    a.textContent=(it.d?"📁 ":"📄 ")+it.n;
    if(it.d){a.href="#";a.onclick=function(){ls(full);return false}}
    else{a.href="/api/dl?path="+E(full)}
    n.appendChild(a);
    if(!it.d){var s=document.createElement("small");s.textContent="  "+sz(it.s);n.appendChild(s)}
    row.appendChild(n);
    var b1=document.createElement("button");b1.className="s";b1.textContent="✎";
    b1.onclick=function(){ren(full,it.n)};
    var b2=document.createElement("button");b2.className="s r";b2.textContent="🗑";
    b2.onclick=function(){del(full,it.n)};
    row.appendChild(b1);row.appendChild(b2);L.appendChild(row);
  });
}
function ls(p){
  cur=p;st("กำลังโหลด...");crumbs();
  fetch("/api/list?path="+E(p),{cache:"no-store"}).then(function(r){
    if(!r.ok)throw 0;return r.json()}).then(function(j){draw(j.items);st("")})
  .catch(function(){st("โหลดรายการไม่ได้")});
}
function post(u,ok,bad){
  fetch(u,{method:"POST"}).then(function(r){if(r.ok)ok();else bad()}).catch(bad);
}
function ren(full,old){
  var n=prompt("เปลี่ยนชื่อเป็น",old);if(!n||n===old)return;
  post("/api/ren?path="+E(full)+"&to="+E(n),function(){ls(cur)},function(){st("เปลี่ยนชื่อไม่ได้ (ชื่อซ้ำหรือไม่ถูกต้อง)")});
}
function del(full,nm){
  if(!confirm("ลบ "+nm+" ?"))return;
  post("/api/del?path="+E(full),function(){ls(cur)},function(){st("ลบไม่ได้ (โฟลเดอร์ต้องว่างก่อน)")});
}
function mk(){
  var n=prompt("ชื่อโฟลเดอร์ใหม่");if(!n)return;
  post("/api/mkdir?path="+E(cur)+"&name="+E(n),function(){ls(cur)},function(){st("สร้างโฟลเดอร์ไม่ได้")});
}
function up(files){
  var q=Array.prototype.slice.call(files),i=0,ok=0;
  function next(){
    if(i>=q.length){st("อัปโหลดเสร็จ "+ok+"/"+q.length+" ไฟล์");$("pb").style.display="none";$("f").value="";ls(cur);return}
    var f=q[i++],x=new XMLHttpRequest(),fd=new FormData();
    fd.append("f",f,f.name);
    x.open("POST","/api/up?path="+E(cur)+"&name="+E(f.name));
    x.upload.onprogress=function(e){
      if(e.lengthComputable){var p=Math.round(e.loaded*100/e.total);
        $("pb").style.display="block";$("pb").firstChild.style.width=p+"%";
        st("("+i+"/"+q.length+") "+f.name+" "+p+"%")}};
    x.onload=function(){if(x.status===200)ok++;next()};
    x.onerror=function(){next()};
    x.send(fd);
  }
  next();
}
ls("/");
</script></body></html>)HTML";

static WebServer *srv = nullptr;
static bool xfBusy = false, xfDirty = false, xfUpOk = false;
static uint32_t xfLast = 0, xfLastReq = 0;
static int xfUp = 0, xfDl = 0, xfOps = 0;
static int xfRoot = 0, xfSta = 0;
static String xfLastName;
static File xfUf;

static bool xfPathOk(const String &p) {
  if (p.length() == 0 || p[0] != '/') return false;
  if (p.indexOf('\\') >= 0) return false;
  if (p.indexOf("/../") >= 0 || p.endsWith("/..") || p.indexOf("/./") >= 0 || p.endsWith("/.")) return false;
  return true;
}
static String xfSafeName(const String &in) {
  int end = in.length();
  if (end > 200) { end = 200; while (end > 0 && (((uint8_t)in[end]) & 0xC0) == 0x80) end--; }
  String o;
  for (int i = 0; i < end; i++) {
    char c = in[i];
    if ((uint8_t)c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    o += c;
  }
  o.trim();
  while (o.length() && o[o.length() - 1] == '.') o.remove(o.length() - 1);
  if (o.length() == 0) o = "file";
  return o;
}
static String xfJoin(const String &dir, const String &nm) {
  String d = dir;
  while (d.length() > 1 && d.endsWith("/")) d.remove(d.length() - 1);
  return d == "/" ? String("/") + nm : d + "/" + nm;
}
static void xfJsonEsc(const String &s, String &out) {
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c < 0x20) out += ' ';
    else out += c;
  }
}
static String xfPct(const String &s) {
  String o;
  char b[4];
  for (unsigned i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.') o += (char)c;
    else { snprintf(b, sizeof(b), "%%%02X", c); o += b; }
  }
  return o;
}

static void hRoot() { xfLastReq = millis(); xfRoot++; xfDirty = true; xfLast = millis(); srv->send_P(200, "text/html; charset=utf-8", XF_PAGE); }
static void hNF() { srv->send(404, "text/plain", "404"); }
static void hList() {
  xfLastReq = millis();
  String p = srv->arg("path");
  if (p == "") p = "/";
  if (!xfPathOk(p)) { srv->send(400, "text/plain", "bad path"); return; }
  File d = SD.open(p.c_str());
  if (!d || !d.isDirectory()) { if (d) d.close(); srv->send(404, "text/plain", "not found"); return; }
  srv->sendHeader("Cache-Control", "no-store");
  srv->setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv->send(200, "application/json; charset=utf-8", "");
  String buf = "{\"items\":[";
  bool first = true;
  int cnt = 0;
  while (cnt < 500) {
    File e = d.openNextFile();
    if (!e) break;
    String nm = e.name();
    bool dir = e.isDirectory();
    size_t sz = dir ? 0 : e.size();
    e.close();
    if (nm.startsWith(".") || nm == "System Volume Information") continue;
    if (!first) buf += ',';
    first = false;
    buf += "{\"n\":\""; xfJsonEsc(nm, buf);
    buf += "\",\"d\":"; buf += (dir ? 1 : 0);
    buf += ",\"s\":"; buf += (unsigned long)sz; buf += '}';
    cnt++;
    if (buf.length() > 1200) { srv->sendContent(buf); buf = ""; }
  }
  d.close();
  buf += "]}";
  srv->sendContent(buf);
  srv->sendContent("");
}
static void hDl() {
  xfLastReq = millis();
  String p = srv->arg("path");
  if (!xfPathOk(p)) { srv->send(400, "text/plain", "bad path"); return; }
  File f = SD.open(p.c_str(), FILE_READ);
  if (!f || f.isDirectory()) { if (f) f.close(); srv->send(404, "text/plain", "not found"); return; }
  String leaf = leafOf(p);
  srv->sendHeader("Content-Disposition", String("attachment; filename*=UTF-8''") + xfPct(leaf));
  xfBusy = true;
  srv->streamFile(f, "application/octet-stream");
  f.close();
  xfBusy = false; xfDl++; xfLastName = leaf; xfDirty = true; xfLast = millis();
}
static void hUpload() {
  HTTPUpload &u = srv->upload();
  xfLastReq = millis();
  if (u.status == UPLOAD_FILE_START) {
    xfBusy = true; xfUpOk = false;
    if (xfUf) xfUf.close();
    String dir = srv->arg("path");
    if (dir == "") dir = "/";
    String nm = srv->arg("name");
    if (nm == "") nm = u.filename;
    nm = xfSafeName(nm);
    if (!xfPathOk(dir)) return;
    xfUf = SD.open(xfJoin(dir, nm).c_str(), FILE_WRITE);
    if (xfUf) xfLastName = nm;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (xfUf && xfUf.write(u.buf, u.currentSize) != u.currentSize) xfUf.close();
  } else if (u.status == UPLOAD_FILE_END) {
    if (xfUf) { xfUf.close(); xfUpOk = true; xfUp++; }
    xfBusy = false; xfDirty = true; xfLast = millis();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (xfUf) xfUf.close();
    xfBusy = false;
  }
}
static void hUpDone() { srv->send(xfUpOk ? 200 : 500, "text/plain", xfUpOk ? "ok" : "fail"); }
static void hRen() {
  xfLastReq = millis();
  String p = srv->arg("path");
  String to = xfSafeName(srv->arg("to"));
  if (!xfPathOk(p) || p == "/") { srv->send(400, "text/plain", "bad"); return; }
  int i = p.lastIndexOf('/');
  String np = xfJoin(i <= 0 ? String("/") : p.substring(0, i), to);
  if (SD.exists(np.c_str())) { srv->send(409, "text/plain", "exists"); return; }
  bool ok = SD.rename(p.c_str(), np.c_str());
  srv->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "fail");
  if (ok) { xfOps++; xfDirty = true; xfLast = millis(); }
}
static void hDel() {
  xfLastReq = millis();
  String p = srv->arg("path");
  if (!xfPathOk(p) || p == "/") { srv->send(400, "text/plain", "bad"); return; }
  File f = SD.open(p.c_str());
  if (!f) { srv->send(404, "text/plain", "not found"); return; }
  bool d = f.isDirectory();
  f.close();
  bool ok = d ? SD.rmdir(p.c_str()) : SD.remove(p.c_str());
  srv->send(ok ? 200 : 409, "text/plain", ok ? "ok" : "fail");
  if (ok) { xfOps++; xfDirty = true; xfLast = millis(); }
}
static void hMk() {
  xfLastReq = millis();
  String dir = srv->arg("path");
  String nm = xfSafeName(srv->arg("name"));
  if (!xfPathOk(dir)) { srv->send(400, "text/plain", "bad"); return; }
  String full = xfJoin(dir, nm);
  bool ok = !SD.exists(full.c_str()) && SD.mkdir(full.c_str());
  srv->send(ok ? 200 : 409, "text/plain", ok ? "ok" : "fail");
  if (ok) { xfOps++; xfDirty = true; xfLast = millis(); }
}

static void xfDraw(bool ap, const String &ssid, const String &pass, const String &ip) {
  header("รับส่งไฟล์ WiFi");
  cv->setTextColor(1);
  thWrapDraw(16, 140, ap ? "1) ต่อ WiFi ของมือถือเข้าเครื่องนี้" : "1) มือถือต้องอยู่ WiFi เดียวกัน", 1, 448, 1);
  thWrapDraw(16, 192, ssid.c_str(), 0, 448, 1);
  if (ap) { String t = "รหัส: " + pass; thWrapDraw(16, 244, t.c_str(), 0, 448, 1); }
  thWrapDraw(16, 306, "2) พิมพ์ในเบราว์เซอร์ (ต้องมี http://)", 1, 448, 1);
  String url = "http://" + ip;
  thWrapDraw(16, 358, url.c_str(), 0, 448, 1);
  char cs[96];
  if (ap) snprintf(cs, sizeof(cs), "อุปกรณ์ที่ต่ออยู่ %d   เปิดหน้าเว็บ %d ครั้ง", xfSta, xfRoot);
  else snprintf(cs, sizeof(cs), "สัญญาณ %d dBm   เปิดหน้าเว็บ %d ครั้ง", (int)WiFi.RSSI(), xfRoot);
  thWrapDraw(16, 410, cs, 1, 448, 1);
  char st[96];
  snprintf(st, sizeof(st), "อัปโหลด %d  ดาวน์โหลด %d  อื่น ๆ %d", xfUp, xfDl, xfOps);
  thWrapDraw(16, 450, st, 1, 448, 1);
  if (xfLastName.length()) thWrapDraw(16, 500, xfLastName.c_str(), 1, 448, 2);
  snprintf(st, sizeof(st), "RAM ว่าง %u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  thWrapDraw(16, 720, st, 1, 448, 1);
  footer("BACK = หยุดและปิด WiFi");
}

static void xfRun(bool ap, const String &ssid, const String &pass) {
  DBG("xf:start");
  SD.end();
  if (!SD.begin(SD_CS, SPI, 20000000)) { msg("รับส่งไฟล์", "อ่าน SD การ์ดไม่ได้"); return; }
  header("รับส่งไฟล์ WiFi");
  thWrapDraw(16, 170, ap ? "กำลังเปิดฮอตสปอต..." : "กำลังเชื่อมต่อ WiFi...", 0, 448, 1);
  thWrapDraw(16, 782, "BACK = ยกเลิก", 1, 340, 1);
  present();
  String ip;
  if (ap) {
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid.c_str(), pass.c_str())) {
      WiFi.mode(WIFI_OFF);
      msg("ล้มเหลว", "เปิดฮอตสปอตไม่ได้", "รหัสต้องยาว 8 ตัวอักษรขึ้นไป");
      return;
    }
    delay(300);
    ip = WiFi.softAPIP().toString();
    WiFi.setSleep(false);
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    bool cancel = false;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
      if (readBtn() == BACK) { cancel = true; break; }
      delay(100);
    }
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
      while (readBtn() != NONE) delay(10);
      heldBtn = NONE; qclear();
      if (!cancel) msg("ต่อ WiFi ไม่ได้", "ตรวจชื่อและรหัสผ่าน", "และอยู่ใกล้เราเตอร์");
      return;
    }
    ip = WiFi.localIP().toString();
    WiFi.setSleep(false);
  }
  DBG("xf:server");
  srv = new WebServer(80);
  srv->on("/", HTTP_GET, hRoot);
  srv->on("/api/list", HTTP_GET, hList);
  srv->on("/api/dl", HTTP_GET, hDl);
  srv->on("/api/up", HTTP_POST, hUpDone, hUpload);
  srv->on("/api/ren", HTTP_POST, hRen);
  srv->on("/api/del", HTTP_POST, hDel);
  srv->on("/api/mkdir", HTTP_POST, hMk);
  srv->onNotFound(hNF);
  srv->begin();
  xfUp = xfDl = xfOps = 0; xfRoot = xfSta = 0; xfLastName = ""; xfDirty = false; xfBusy = false;
  xfLastReq = millis();
  xfDraw(ap, ssid, pass, ip);
  present();
  int backCnt = 0;
  uint32_t pwrSince = 0;
  bool sleepNow = false, timeout = false;
  while (true) {
    srv->handleClient();
    uint32_t now = millis();
    int b = readBtn();
    backCnt = (b == BACK) ? backCnt + 1 : 0;
    if (backCnt >= 6) break;
    if (b == POWER) {
      if (!pwrSince) pwrSince = now;
      if (now - pwrSince >= SLEEP_HOLD_MS) { sleepNow = true; break; }
    } else pwrSince = 0;
    if (ap) { int n = WiFi.softAPgetStationNum(); if (n != xfSta) { xfSta = n; xfDirty = true; xfLast = now; } }
    if (xfDirty && !xfBusy && now - xfLast > 1200) {
      xfDirty = false;
      xfDraw(ap, ssid, pass, ip);
      present();
    }
    idleTick();
    if (now - xfLastReq > 20UL * 60000UL) { timeout = true; break; }
    delay(5);
  }
  srv->stop();
  delete srv; srv = nullptr;
  if (ap) WiFi.softAPdisconnect(true); else WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  while (readBtn() != NONE) delay(10);
  heldBtn = NONE; qclear();
  if (sleepNow) goSleep();
  if (timeout) msg("รับส่งไฟล์", "หยุดทำงานอัตโนมัติ", "(ไม่มีการใช้งาน 20 นาที)");
}

static void wifiSetupHome() {
  DBG("wifi:scan");
  header("ค้นหา WiFi");
  thDraw(16, 170, "กำลังค้นหา...", 0);
  present();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  static String names[14];
  int m = 0;
  names[m++] = "[พิมพ์ชื่อเอง]";
  for (int i = 0; i < n && m < 14; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (int j = 0; j < m; j++) if (names[j] == s) dup = true;
    if (!dup) names[m++] = s;
  }
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  DBG("wifi:pick");
  int s = menu("เลือก WiFi บ้าน", names, m, 3, "OK=เลือก  BACK=ยกเลิก");
  if (s < 0) return;
  String ssid, pass;
  if (s == 0) {
    if (!kbInput("ชื่อ WiFi (SSID)", ssid, 32, 0, "พิมพ์ชื่อให้ตรงทุกตัวอักษร")) return;
    if (ssid.length() == 0) return;
  } else ssid = names[s];
  DBG("wifi:kbpass");
  if (!kbInput("รหัสผ่าน WiFi", pass, 63, 0, ssid)) return;
  DBG("wifi:save");
  Preferences p;
  bool okp = p.begin("wifi", false);
  size_t wn = okp ? p.putString("ssid", ssid) : 0;
  if (okp) p.putString("pass", pass);
  if (okp) p.end();
  DBG("wifi:saved");
  if (!okp || wn == 0) msg("บันทึกไม่สำเร็จ", "เขียนหน่วยความจำ (NVS) ไม่ได้");
  else msg("บันทึกแล้ว", ssid, "เลือก 'ต่อ WiFi' เพื่อทดสอบ");
}

static void wifiMenu() {
  int sel = 0;
  while (true) {
    Preferences p;
    p.begin("wifi", false);
    String hs = p.getString("ssid", "");
    String hp = p.getString("pass", "");
    String ap = p.getString("appass", "clover1234");
    p.end();
    String items[4];
    items[0] = "เปิดรับส่งไฟล์: ฮอตสปอตของเครื่อง";
    items[1] = hs.length() ? String("เปิดรับส่งไฟล์: ต่อ ") + hs : String("เปิดรับส่งไฟล์: ต่อ WiFi บ้าน (ยังไม่ตั้งค่า)");
    items[2] = "ตั้งค่า WiFi บ้าน (ชื่อ + รหัสผ่าน)";
    items[3] = "ตั้งรหัสฮอตสปอต";
    sel = menu("รับส่งไฟล์ WiFi", items, 4, 3, "OK=เลือก  BACK=กลับ", sel);
    if (sel < 0) return;
    if (sel == 0) xfRun(true, "Clover-X4", ap);
    else if (sel == 1) {
      if (hs.length() == 0) msg("ยังไม่ได้ตั้งค่า", "เลือกเมนู 'ตั้งค่า WiFi บ้าน' ก่อน");
      else xfRun(false, hs, hp);
    } else if (sel == 2) wifiSetupHome();
    else if (sel == 3) {
      String t = ap;
      if (kbInput("รหัสฮอตสปอต", t, 40, 0, "ชื่อเครือข่าย Clover-X4 (8 ตัวขึ้นไป)")) {
        if (t.length() < 8 || t.length() > 63) msg("รหัสไม่ถูกต้อง", "ต้องยาว 8-63 ตัวอักษร");
        else {
          Preferences q;
          q.begin("wifi", false);
          q.putString("appass", t);
          q.end();
          msg("บันทึกแล้ว", t);
        }
      }
    }
  }
}

// ---------- ตั้งค่า (ไอคอนเฟือง) ----------
static const int FE_OPT[4] = {0, 10, 30, 60};
static void settingsSave() {
  Preferences q;
  q.begin("ui", false);
  q.putInt("fe", fullEvery);
  q.end();
}
static void settingsMenu() {
  int sel = 0;
  while (true) {
    String items[3];
    items[0] = fullEvery == 0 ? String("รีเฟรชเต็มจออัตโนมัติ: ปิด")
                              : String("รีเฟรชเต็มจออัตโนมัติ: ทุก ") + String(fullEvery) + " ครั้ง";
    items[1] = "รีเฟรชจอเต็มตอนนี้ (หรือกด OK ค้างที่หน้าไหนก็ได้)";
    items[2] = "ล้างค่า WiFi บ้านที่บันทึกไว้";
    sel = menu("ตั้งค่า", items, 3, 3, "OK=เลือก  BACK=กลับ", sel);
    if (sel < 0) return;
    if (sel == 0) {
      int k = 0;
      for (int i = 0; i < 4; i++) if (FE_OPT[i] == fullEvery) k = i;
      fullEvery = FE_OPT[(k + 1) % 4];
      settingsSave();
    } else if (sel == 1) {
      present(true);
    } else if (sel == 2) {
      Preferences q;
      q.begin("wifi", false);
      q.remove("ssid");
      q.remove("pass");
      q.end();
      msg("ล้างแล้ว", "ลบค่า WiFi บ้านที่บันทึกไว้แล้ว");
    }
  }
}
