#pragma once
// thai_wb.h : หาจุดตัดคำไทยด้วยพจนานุกรม (trie ใน thai_dict.h) ระดับ cluster
// ใช้ร่วมกับ thSegment: wb[k] = true เมื่อ "ตัดบรรทัดก่อน cluster k ได้" (ระหว่างคำ)
// ตัวอักษรที่ไม่ใช่ไทยได้ wb = true เสมอ (กฎเดิม noBreakBefore ยังใช้ควบคู่)
#include <stdint.h>

#define THW_WORD_COST 1     // ราคาต่อคำในพจนานุกรม
#define THW_UNK_COST 5      // ราคาต่อ cluster ที่ไม่รู้จัก (คำนอกพจนานุกรมจะถูกรวมเป็นก้อนเดียวไม่ตัดกลาง)

static inline bool thwThai(const char *s, const ThCl &c) {
  int u; uint32_t cp = thDecode((const uint8_t *)s + c.start, c.len, u);
  return cp >= 0x0E01 && cp <= 0x0E5B;
}
// ลูกของโหนด node (-1 = ราก) ที่มีรหัส ch; -1 ถ้าไม่มี
static int thwChild(int node, uint32_t ch) {
  int end, idx;
  if (node < 0) { idx = 0; end = TH_DICT_N; }
  else {
    uint32_t sz = TH_DICT[node] >> 8;
    if (sz <= 1) return -1;
    idx = node + 1; end = node + (int)sz;
  }
  while (idx < end) {
    uint32_t v = TH_DICT[idx], c = v & 0x7F;
    if (c == ch) return idx;
    if (c > ch) return -1;              // พี่น้องเรียงจากน้อยไปมาก
    idx += (int)(v >> 8);
  }
  return -1;
}
// ตัดคำช่วง cluster [a,b) ที่เป็นไทยล้วน
static void thwRun(const char *s, const ThCl *cl, int a, int b, bool *wb) {
  const int m = b - a;
  static int16_t cost[TH_MAXC + 1];
  static int16_t from[TH_MAXC + 1];     // จุดเริ่มของโทเค็นที่จบที่ตำแหน่งนี้
  static bool unk[TH_MAXC + 1];         // โทเค็นนี้เป็น "ไม่รู้จัก" หรือไม่
  for (int i = 0; i <= m; i++) { cost[i] = 32000; from[i] = -1; unk[i] = false; }
  cost[0] = 0;
  for (int i = 0; i < m; i++) {
    if (cost[i] >= 32000) continue;
    int c = cost[i] + THW_UNK_COST;
    if (c < cost[i + 1]) { cost[i + 1] = c; from[i + 1] = i; unk[i + 1] = true; }
    int node = -1;
    for (int t = i; t < m && node >= -1; t++) {
      const ThCl &k = cl[a + t];
      int p = 0;
      while (p < k.len) {
        int u; uint32_t cp = thDecode((const uint8_t *)s + k.start + p, k.len - p, u);
        p += u;
        node = thwChild(node, cp - 0x0E00);
        if (node < 0) break;
      }
      if (node < 0) break;
      if (TH_DICT[node] & 0x80) {
        int cc = cost[i] + THW_WORD_COST;
        if (cc < cost[t + 1]) { cost[t + 1] = cc; from[t + 1] = i; unk[t + 1] = false; }
      }
    }
  }
  for (int k = 0; k < m; k++) wb[a + k] = false;
  int e = m;
  while (e > 0) {
    int st = from[e];
    bool u = unk[e];
    // รวมโทเค็นไม่รู้จักที่ติดกันเป็นก้อนเดียว
    while (u && st > 0 && unk[st]) st = from[st];
    wb[a + st] = true;
    e = st;
  }
}
static void thWordBreaks(const char *s, const ThCl *cl, int n, bool *wb) {
  for (int k = 0; k < n; k++) wb[k] = true;
  int i = 0;
  while (i < n) {
    if (!thwThai(s, cl[i])) { i++; continue; }
    int j = i;
    while (j < n && thwThai(s, cl[j])) j++;
    thwRun(s, cl, i, j, wb);
    i = j;
  }
}
