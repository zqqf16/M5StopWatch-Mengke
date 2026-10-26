#!/bin/bash
# Build the Chinese subset fonts (ZCOOL KuaiLe, OFL) used by the Mengke apps.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=main/assets/mengke
CH=$(python3 - <<'P'
import glob,json,re
t=''.join(open(f,encoding='utf8').read() for f in glob.glob('main/apps/mengke/*.cpp')+['tools/mengke.json','main/main.cpp','main/hal/hal.h',])
t+='一二三四五六七八九十月日周'
s=sorted({c for c in t if ord(c)>0x7e and c not in '♥★'})
print(''.join(s))
P
)
echo "CJK glyphs: ${#CH}"
FONT=tools/fonts/ZCOOLKuaiLe-Regular.ttf
for sz in 26 40; do
  lv_font_conv --bpp 4 --size $sz --no-compress --format lvgl --lv-include lvgl.h \
    --font $FONT -r 0x20-0x7E --symbols "$CH" \
    --font /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf --symbols "♥★" \
    -o $OUT/mengke_font_$sz.c --lv-font-name mengke_font_$sz
done
lv_font_conv --bpp 4 --size 100 --no-compress --format lvgl --lv-include lvgl.h --font $FONT --symbols "0123456789:-" \
  -o $OUT/mengke_font_digits.c --lv-font-name mengke_font_digits
ls -la $OUT/*.c
