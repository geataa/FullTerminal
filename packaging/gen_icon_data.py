#!/usr/bin/env python3
# resources/app.png -> src/platform/linux/AppIconData.h (128x128 ARGB).
# Linux pencere/tepsi simgesi bu gomulu veriden uretilir (libpng bagimliligi yok).
from PIL import Image
import os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 128
im = Image.open(os.path.join(root, 'resources/app.png')).convert('RGBA').resize((S, S), Image.LANCZOS)
vals = [(a << 24) | (r << 16) | (g << 8) | b for (r, g, b, a) in im.getdata()]
lines = ['    ' + ', '.join('0x%08X' % v for v in vals[i:i + 10]) + ',' for i in range(0, len(vals), 10)]
out = '''// Otomatik uretildi: resources/app.png -> 128x128 ARGB (0xAARRGGBB, on carpimsiz).
// Yeniden uretmek icin: packaging/gen_icon_data.py
#pragma once
#include <cstdint>

namespace ft::lnx {
constexpr int kAppIconSize = %d;
inline constexpr uint32_t kAppIconArgb[kAppIconSize * kAppIconSize] = {
%s
};
} // namespace ft::lnx
''' % (S, '\n'.join(lines))
with open(os.path.join(root, 'src/platform/linux/AppIconData.h'), 'w') as f:
    f.write(out)
