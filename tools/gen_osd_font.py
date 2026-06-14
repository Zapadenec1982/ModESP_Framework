#!/usr/bin/env python3
"""
gen_osd_font.py — генератор шрифту character-NVM для AT7456E / MAX7456.

Рендерить TTF у гліфи 12×18px (2 bpp) і пакує у формат .mcm
(зворотний до bri3d/mcm2img) + C-масив для заливки з прошивки.

Розкладка гліфів МАЄ збігатися з
  components/modesp_osd/include/modesp/osd/osd_charmap.h:
    0x20-0x7E  друкований ASCII (індекс = код)
    0x7F       '°'
    0x80-0xBF  кирилиця U+0410..U+044F (А-я)
    0xC0-0xC7  Є І Ї Ґ є і ї ґ

Формат .mcm (Maxim AN4117, як читає mcm2img):
  рядок "MAX7456", далі 256 символів по 64 рядки;
  кожен рядок = 8 двійкових цифр = 1 байт = 4 пікселі (2 bpp, MSB-pair = лівий);
  54 рядки даних (18 рядів × 3 байти) + 10 рядків паддінгу.
  Кодування пікселя: 00=чорний 01=прозорий 10=білий 11=прозорий.

Вивід:
  components/modesp_osd/font/osd_font.mcm            (для інспекції mcm2img)
  components/modesp_osd/include/modesp/osd/osd_font_data.h  (для прошивки)

Приклад:
  python tools/gen_osd_font.py --ttf C:/Windows/Fonts/consola.ttf --size 16
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow не встановлено: pip install Pillow")

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = Path(__file__).parent.parent

CHAR_W, CHAR_H = 12, 18
CHAR_BYTES = CHAR_W * CHAR_H * 2 // 8   # 54
PAD_LINES = 10                          # до 64 рядків/символ
CHAR_COUNT = 256

# Версійний sentinel: байт0 гліфа SENTINEL_ADDR. Бампни при зміні шрифту,
# щоб прошивка перезалила NVM (інакше пропускає за збігом).
SENTINEL_ADDR = 0xFF
SENTINEL_BYTE0 = 0xAB

# Піксельні коди (2 bpp)
PX_BLACK = "00"
PX_TRANSPARENT = "01"
PX_WHITE = "10"


def cp_to_glyph(cp):
    """codepoint → індекс гліфа (= osd_charmap.h). None якщо поза набором."""
    if 0x20 <= cp <= 0x7E:
        return cp
    if 0x0410 <= cp <= 0x044F:
        return 0x80 + (cp - 0x0410)
    return {
        0x00B0: 0x7F,
        0x0404: 0xC0, 0x0406: 0xC1, 0x0407: 0xC2, 0x0490: 0xC3,
        0x0454: 0xC4, 0x0456: 0xC5, 0x0457: 0xC6, 0x0491: 0xC7,
    }.get(cp)


def charset():
    """Усі codepoint-и нашого набору (унікальні)."""
    cps = list(range(0x20, 0x7F))           # ASCII
    cps.append(0x00B0)                       # °
    cps += list(range(0x0410, 0x0450))       # А-я
    cps += [0x0404, 0x0406, 0x0407, 0x0490,  # Є І Ї Ґ
            0x0454, 0x0456, 0x0457, 0x0491]   # є і ї ґ
    return cps


def render_glyph(ch, font, x_off, y_off, thr, outline):
    """Намалювати символ → матриця 18×12 кодів пікселів (PX_*)."""
    img = Image.new("L", (CHAR_W, CHAR_H), 0)
    draw = ImageDraw.Draw(img)
    draw.text((x_off, y_off), ch, fill=255, font=font)
    px = img.load()

    fg = [[px[x, y] > thr for x in range(CHAR_W)] for y in range(CHAR_H)]

    rows = []
    for y in range(CHAR_H):
        row = []
        for x in range(CHAR_W):
            if fg[y][x]:
                row.append(PX_WHITE)
            elif outline and _is_edge(fg, x, y):
                row.append(PX_BLACK)        # чорний ореол для читабельності
            else:
                row.append(PX_TRANSPARENT)
        rows.append(row)
    return rows


def _is_edge(fg, x, y):
    """true якщо піксель — фон, але сусідить з переднім планом (для ореолу)."""
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            nx, ny = x + dx, y + dy
            if 0 <= nx < CHAR_W and 0 <= ny < CHAR_H and fg[ny][nx]:
                return True
    return False


def pack_char(rows):
    """18×12 кодів пікселів → 54 байти (MSB-pair = лівий піксель)."""
    if rows is None:
        rows = [[PX_TRANSPARENT] * CHAR_W for _ in range(CHAR_H)]
    data = bytearray()
    bits = "".join("".join(r) for r in rows)   # 18*12*2 = 432 біт
    for i in range(0, len(bits), 8):
        data.append(int(bits[i:i + 8], 2))
    assert len(data) == CHAR_BYTES, len(data)
    return data


def mcm_lines(char_data):
    """54 байти → 64 рядки .mcm (54 дані + 10 паддінг, '01010101')."""
    out = []
    for b in char_data:
        out.append(format(b, "08b"))
    out += ["01010101"] * PAD_LINES
    return out


def main():
    ap = argparse.ArgumentParser(description="AT7456E/MAX7456 font generator")
    ap.add_argument("--ttf", default="C:/Windows/Fonts/consola.ttf",
                    help="шлях до TTF (моноширинний з кирилицею)")
    ap.add_argument("--size", type=int, default=16, help="кегль рендера")
    ap.add_argument("--x-off", type=int, default=1, help="зсув X у клітинці")
    ap.add_argument("--y-off", type=int, default=-1, help="зсув Y у клітинці")
    ap.add_argument("--threshold", type=int, default=110, help="поріг 0..255")
    ap.add_argument("--no-outline", action="store_true", help="без чорного ореолу")
    ap.add_argument("--preview", metavar="PNG",
                    help="зберегти PNG-превʼю атласу (для звірки)")
    args = ap.parse_args()

    try:
        font = ImageFont.truetype(args.ttf, args.size)
    except OSError:
        sys.exit(f"не вдалось відкрити TTF: {args.ttf}")

    # 256 слотів; за замовчуванням прозорий blank
    glyphs = [None] * CHAR_COUNT
    for cp in charset():
        idx = cp_to_glyph(cp)
        if idx is None:
            continue
        glyphs[idx] = render_glyph(chr(cp), font, args.x_off, args.y_off,
                                   args.threshold, not args.no_outline)

    packed = [pack_char(g) for g in glyphs]

    # Версійний sentinel у байт0 гліфа SENTINEL_ADDR
    packed[SENTINEL_ADDR][0] = SENTINEL_BYTE0

    # ── .mcm ──
    mcm_path = ROOT / "components" / "modesp_osd" / "font" / "osd_font.mcm"
    mcm_path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["MAX7456"]
    for cd in packed:
        lines += mcm_lines(cd)
    mcm_path.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"  + {mcm_path} ({len(packed)} chars)")

    # ── C-масив ──
    h_path = (ROOT / "components" / "modesp_osd" / "include" / "modesp" /
              "osd" / "osd_font_data.h")
    out = [
        "#pragma once",
        "// Auto-generated by tools/gen_osd_font.py — DO NOT EDIT",
        f"// Шрифт {CHAR_COUNT} гліфів × {CHAR_BYTES} байт для AT7456E character-NVM.",
        "// Розкладка = modesp/osd/osd_charmap.h.",
        "",
        "#include <cstdint>",
        "#include <cstddef>",
        "",
        "namespace modesp::osd {",
        "",
        f"static constexpr size_t  OSD_FONT_CHARS = {CHAR_COUNT};",
        f"static constexpr size_t  OSD_FONT_CHAR_BYTES = {CHAR_BYTES};",
        f"static constexpr int     OSD_FONT_SENTINEL_ADDR = 0x{SENTINEL_ADDR:02X};",
        f"static constexpr uint8_t OSD_FONT_SENTINEL_BYTE0 = 0x{SENTINEL_BYTE0:02X};",
        "",
        f"static constexpr uint8_t OSD_FONT[{CHAR_COUNT} * {CHAR_BYTES}] = {{",
    ]
    for idx, cd in enumerate(packed):
        body = ", ".join(f"0x{b:02X}" for b in cd)
        out.append(f"    {body},  // 0x{idx:02X}")
    out += ["};", "", "} // namespace modesp::osd", ""]
    h_path.write_text("\n".join(out), encoding="utf-8")
    print(f"  + {h_path}")

    # ── превʼю ──
    if args.preview:
        cols = 16
        rows_n = (CHAR_COUNT + cols - 1) // cols
        atlas = Image.new("RGB", (cols * (CHAR_W + 1), rows_n * (CHAR_H + 1)),
                          (40, 40, 40))
        for idx, g in enumerate(glyphs):
            if g is None:
                continue
            cx = (idx % cols) * (CHAR_W + 1)
            cy = (idx // cols) * (CHAR_H + 1)
            for y in range(CHAR_H):
                for x in range(CHAR_W):
                    code = g[y][x]
                    if code == PX_WHITE:
                        atlas.putpixel((cx + x, cy + y), (255, 255, 255))
                    elif code == PX_BLACK:
                        atlas.putpixel((cx + x, cy + y), (0, 0, 0))
        atlas.save(args.preview)
        print(f"  + {args.preview}")

    print(f"\nГотово: {sum(1 for g in glyphs if g)} гліфів відрендерено.")


if __name__ == "__main__":
    main()
