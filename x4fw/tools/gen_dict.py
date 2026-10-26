#!/usr/bin/env python3
# สร้าง src/thai_dict.h : พจนานุกรมคำไทยแบบ trie บีบอัด (ฝังใน firmware ใช้ตัดคำตอนจัดบรรทัด)
# ใช้:  pip install pythainlp  แล้ว  python3 tools/gen_dict.py
#   --words ไฟล์.txt   ใช้รายการคำของคุณเองแทน pythainlp (1 คำต่อบรรทัด)
#   tools/th_extra_words.txt  คำเพิ่มเอง (ชื่อเฉพาะ ศัพท์ในหนังสือ) 1 คำต่อบรรทัด # = คอมเมนต์
import sys, os, argparse

ap = argparse.ArgumentParser()
ap.add_argument('--words'); ap.add_argument('--extra', default='tools/th_extra_words.txt')
ap.add_argument('--out', default='src/thai_dict.h')
a = ap.parse_args()

def read_lines(p):
    with open(p, encoding='utf-8') as f:
        return [l.strip() for l in f if l.strip() and not l.lstrip().startswith('#')]

if a.words:
    words = read_lines(a.words)
else:
    try:
        from pythainlp.corpus import thai_words
        words = list(thai_words())
    except Exception as e:
        print('ไม่พบ pythainlp (%s) -> รัน: pip install pythainlp' % e); sys.exit(1)
    if len(words) < 10000:
        print('รายการคำจาก pythainlp น้อยผิดปกติ (%d)' % len(words)); sys.exit(1)
if os.path.exists(a.extra): words += read_lines(a.extra)

def good(w):
    if len(w) < 2: return False
    if any(not (0x0E01 <= ord(c) <= 0x0E5B) for c in w): return False
    c0, cl = ord(w[0]), ord(w[-1])
    if not (0x0E01 <= c0 <= 0x0E2E or 0x0E40 <= c0 <= 0x0E44): return False   # ต้องขึ้นต้นด้วยพยัญชนะ/สระหน้า
    if 0x0E40 <= cl <= 0x0E44: return False                                  # ห้ามลงท้ายด้วยสระหน้า
    return True
ws = sorted({w for w in words if good(w)})
print('คำที่ใช้ได้: %d จาก %d' % (len(ws), len(words)))

# trie -> อาร์เรย์ preorder: แต่ละโหนด = ch(7 บิต) | terminal<<7 | ขนาดซับทรี<<8
root = {}
for w in ws:
    n = root
    for c in w: n = n.setdefault(ord(c) - 0xE00, {})
    n[0] = True        # 0 = ธงจบคำ (ไม่ชนกับรหัสตัวอักษร 1..91)
out = []
def emit(node):
    for ch in sorted(k for k in node if k != 0):
        idx = len(out); out.append(0)
        child = node[ch]
        emit(child)
        term = 1 if 0 in child else 0
        size = len(out) - idx
        assert size < (1 << 24)
        out[idx] = ch | (term << 7) | (size << 8)
emit(root)
with open(a.out, 'w') as f:
    f.write('// สร้างโดย tools/gen_dict.py : trie คำไทย %d คำ, %d โหนด\n#pragma once\n#include <stdint.h>\n' % (len(ws), len(out)))
    f.write('#define TH_DICT_N %d\nstatic const uint32_t TH_DICT[TH_DICT_N] = {\n' % len(out))
    for i in range(0, len(out), 10):
        f.write(','.join('0x%X' % v for v in out[i:i + 10]) + ',\n')
    f.write('};\n')
print('เขียน %s : %d โหนด (~%d KB ในแฟลช)' % (a.out, len(out), len(out) * 4 // 1024))
