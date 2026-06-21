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
SENTINEL_BYTE0 = 0xAC   # bump: додано PL/DE-гліфи (0xC8-0xE0) → прошивка перезаливає NVM

# Піксельні коди (2 bpp)
PX_BLACK = "00"
PX_TRANSPARENT = "01"
PX_WHITE = "10"

# Польські + німецькі спецлітери (i18n pl/de). СПІЛЬНИЙ порядок для обох цілей:
# AT7456E індекс = AT_EXT_BASE + i; AMT630A tile = 0x268 + i.
EXT_LATIN = [
    0x00C4, 0x00D6, 0x00DC, 0x00E4, 0x00F6, 0x00FC, 0x00DF,   # Ä Ö Ü ä ö ü ß
    0x0104, 0x0105, 0x0106, 0x0107, 0x0118, 0x0119,           # Ą ą Ć ć Ę ę
    0x0141, 0x0142, 0x0143, 0x0144, 0x00D3, 0x00F3,           # Ł ł Ń ń Ó ó
    0x015A, 0x015B, 0x0179, 0x017A, 0x017B, 0x017C,           # Ś ś Ź ź Ż ż
]
AT_EXT_BASE = 0xC8   # AT7456E: PL/DE-індекси 0xC8..0xE0


def cp_to_glyph(cp):
    """codepoint → індекс гліфа (= osd_charmap.h). None якщо поза набором."""
    if 0x20 <= cp <= 0x7E:
        return cp
    if 0x0410 <= cp <= 0x044F:
        return 0x80 + (cp - 0x0410)
    table = {
        0x00B0: 0x7F,
        0x0404: 0xC0, 0x0406: 0xC1, 0x0407: 0xC2, 0x0490: 0xC3,
        0x0454: 0xC4, 0x0456: 0xC5, 0x0457: 0xC6, 0x0491: 0xC7,
    }
    table.update({c: AT_EXT_BASE + i for i, c in enumerate(EXT_LATIN)})
    return table.get(cp)


def charset():
    """Усі codepoint-и нашого набору (унікальні)."""
    cps = list(range(0x20, 0x7F))           # ASCII
    cps.append(0x00B0)                       # °
    cps += list(range(0x0410, 0x0450))       # А-я
    cps += [0x0404, 0x0406, 0x0407, 0x0490,  # Є І Ї Ґ
            0x0454, 0x0456, 0x0457, 0x0491]   # є і ї ґ
    cps += EXT_LATIN                          # PL/DE
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


# ── AMT630A target: 16×22 1bpp RAM-font (ADR-002 / AMT630A_driver_design.md §4) ──
AMT_W, AMT_H = 16, 20   # 20 рядів: повний uk+pl+de набір (193 гліфи × 20 = 3860) влазить у 4096-слівну FONT RAM
AMT_FIRST_TILE = 0x1C0   # BGMAP-код першого RAM-гліфа


def amt630a_entries():
    """[(codepoint, bgmap_tile)] у порядку зростання tile (= розкладка amt630a_charmap.h)."""
    out, t = [], AMT_FIRST_TILE
    for cp in range(0x20, 0x7F):            # ASCII space..~  (95)
        out.append((cp, t)); t += 1
    out.append((0x00B0, t)); t += 1          # °
    for cp in range(0x0410, 0x0450):         # А-я  (64)
        out.append((cp, t)); t += 1
    for cp in (0x0404, 0x0406, 0x0407, 0x0490,   # Є І Ї Ґ
               0x0454, 0x0456, 0x0457, 0x0491):  # є і ї ґ
        out.append((cp, t)); t += 1
    for cp in EXT_LATIN:                          # PL/DE (tile 0x268+)
        out.append((cp, t)); t += 1
    return out


def render_glyph_1bpp(ch, font, x_off, y_off, thr):
    """Символ → AMT_H слів по 16 біт (MSB-first: bit15 = крайній лівий піксель)."""
    img = Image.new("L", (AMT_W, AMT_H), 0)
    ImageDraw.Draw(img).text((x_off, y_off), ch, fill=255, font=font)
    px = img.load()
    words = []
    for y in range(AMT_H):
        w = 0
        for x in range(AMT_W):
            if px[x, y] > thr:
                w |= 1 << (15 - x)
        words.append(w)
    return words


def generate_amt630a(args):
    try:
        font = ImageFont.truetype(args.ttf, args.size)
    except OSError:
        sys.exit(f"не вдалось відкрити TTF: {args.ttf}")

    entries = amt630a_entries()
    words = []
    for cp, _tile in entries:
        words += render_glyph_1bpp(chr(cp), font, args.x_off, args.y_off, args.threshold)
    count = len(entries)

    base = ROOT / "components" / "modesp_osd" / "include" / "modesp" / "osd"

    # ── font data (uint16_t[count*AMT_H]) ──
    fp = base / "amt630a_font_data.h"
    out = [
        "#pragma once",
        "// Auto-generated by tools/gen_osd_font.py --target amt630a — DO NOT EDIT",
        f"// {count} гліфів × {AMT_H} слів (16×22 1bpp, MSB-first, bit15=лівий піксель).",
        "// BGMAP-код = AMT630A_FONT_FIRST_TILE + index; RAM word-tile (upload_font) = index.",
        "// ⚠ bit-order/код↔RAM-word — bench-pending (AMT630A_driver_design.md §9, §12.5).",
        "",
        "#include <cstdint>",
        "#include <cstddef>",
        "",
        "namespace modesp::osd {",
        "",
        f"static constexpr uint16_t AMT630A_FONT_FIRST_TILE = 0x{AMT_FIRST_TILE:03X};",
        f"static constexpr size_t   AMT630A_FONT_COUNT = {count};",
        f"static constexpr uint8_t  AMT630A_FONT_XSIZ = {AMT_W};",
        f"static constexpr uint8_t  AMT630A_FONT_YSIZ = {AMT_H};",
        "",
        f"static constexpr uint16_t AMT630A_FONT[{count} * {AMT_H}] = {{",
    ]
    for i, (cp, tile) in enumerate(entries):
        chunk = words[i * AMT_H:(i + 1) * AMT_H]
        body = ", ".join(f"0x{w:04X}" for w in chunk)
        out.append(f"    {body},  // 0x{tile:03X} U+{cp:04X}")
    out += ["};", "", "} // namespace modesp::osd", ""]
    fp.write_text("\n".join(out), encoding="utf-8")
    print(f"  + {fp} ({count} glyphs)")

    # ── charmap (UTF-8 cp → BGMAP tile) ──
    cp_path = base / "amt630a_charmap.h"
    # switch для не-діапазонних символів (°, укр., PL/DE) — будуємо з entries
    specials = [(c, tile) for c, tile in entries
                if not (0x20 <= c <= 0x7E) and not (0x0410 <= c <= 0x044F)]
    switch_lines = ["    switch (cp) {"]
    for c, tile in specials:
        switch_lines.append(f"        case 0x{c:04X}: return 0x{tile:03X};")
    switch_lines.append("    }")
    cm = [
        "#pragma once",
        "// Auto-generated by tools/gen_osd_font.py --target amt630a — DO NOT EDIT",
        "// UTF-8 codepoint → BGMAP tile-код (RAM-шрифт 0x1C0+). Невідоме → space (0x1C0).",
        "",
        "#include <cstdint>",
        "",
        "namespace modesp::osd {",
        "",
        "inline uint16_t amt630a_cp_to_tile(uint32_t cp) {",
        "    if (cp >= 0x20 && cp <= 0x7E)     return static_cast<uint16_t>(0x1C0u + (cp - 0x20));",
        "    if (cp >= 0x0410 && cp <= 0x044F) return static_cast<uint16_t>(0x220u + (cp - 0x0410));",
        *switch_lines,
        "    return 0x1C0;  // невідоме → space",
        "}",
        "",
        "} // namespace modesp::osd",
        "",
    ]
    cp_path.write_text("\n".join(cm), encoding="utf-8")
    print(f"  + {cp_path}")
    print(f"\nГотово (amt630a): {count} гліфів, перший tile 0x{AMT_FIRST_TILE:03X}.")


# ── Panel target: iPixel/LED_BLE 8×16 1bpp glyphs for the text-frame glyph blocks ──
# Verified vs pypixelcolor encode_char_img + per-byte bit-reverse: the final glyph byte
# has bit0 = LEFTMOST pixel (LSB-first). 16 rows × 1 byte = 16 bytes/glyph (16 tall × 8
# wide cell, block type 0x00). Glyph block on the wire = [0x00][fg RGB][these 16 bytes].
PANEL_W, PANEL_H = 8, 16


def render_glyph_panel(ch, font, x_off, y_off, thr):
    """Char → PANEL_H bytes; bit0 = leftmost pixel (x=0), LSB-first."""
    img = Image.new("L", (PANEL_W, PANEL_H), 0)
    ImageDraw.Draw(img).text((x_off, y_off), ch, fill=255, font=font)
    px = img.load()
    rows = []
    for y in range(PANEL_H):
        b = 0
        for x in range(PANEL_W):
            if px[x, y] > thr:
                b |= (1 << x)        # bit0 = leftmost pixel
        rows.append(b)
    return rows


def generate_panel(args):
    try:
        font = ImageFont.truetype(args.ttf, args.size)
    except OSError:
        sys.exit(f"не вдалось відкрити TTF: {args.ttf}")

    cps = list(range(0x20, 0x7F)) + [0x00B0]   # ASCII printable + °
    rows = []
    for cp in cps:
        rows += render_glyph_panel(chr(cp), font, args.x_off, args.y_off, args.threshold)
    count = len(cps)

    out_dir = ROOT / "generated"
    out_dir.mkdir(exist_ok=True)
    fp = out_dir / "panel_font_data.h"
    out = [
        "#pragma once",
        "// Auto-generated by tools/gen_osd_font.py --target panel — DO NOT EDIT",
        f"// {count} гліфів × {PANEL_H} байт ({PANEL_W}×{PANEL_H} 1bpp, LSB-first: bit0 = лівий піксель).",
        "// iPixel text-frame glyph block = [0x00][fg RGB][ці 16 байт]. ASCII 0x20-0x7E + ° (U+00B0).",
        "",
        "#include <cstdint>",
        "#include <cstddef>",
        "",
        "namespace modesp::panel {",
        "",
        f"static constexpr uint8_t  PANEL_FONT_W = {PANEL_W};",
        f"static constexpr uint8_t  PANEL_FONT_H = {PANEL_H};",
        f"static constexpr size_t   PANEL_FONT_COUNT = {count};",
        "",
        f"static constexpr uint8_t PANEL_FONT[{count} * {PANEL_H}] = {{",
    ]
    for i, cp in enumerate(cps):
        chunk = rows[i * PANEL_H:(i + 1) * PANEL_H]
        body = ", ".join(f"0x{b:02X}" for b in chunk)
        glyph = chr(cp) if 0x20 < cp < 0x7F else " "
        out.append(f"    {body},  // U+{cp:04X} '{glyph}'")
    out += [
        "};",
        "",
        "// codepoint → glyph index (unknown → 0 = space).",
        "inline uint8_t panel_font_index(uint32_t cp) {",
        "    if (cp >= 0x20 && cp <= 0x7E) return static_cast<uint8_t>(cp - 0x20);",
        f"    if (cp == 0x00B0)            return {count - 1};  // °",
        "    return 0;  // space",
        "}",
        "",
        "} // namespace modesp::panel",
        "",
    ]
    fp.write_text("\n".join(out), encoding="utf-8")
    print(f"  + {fp} ({count} glyphs, {PANEL_W}x{PANEL_H} LSB-first)")
    print(f"\nГотово (panel): {count} гліфів.")


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
    ap.add_argument("--target", choices=["at7456e", "amt630a", "panel"], default="at7456e",
                    help="ціль: at7456e (12×18 2bpp NVM), amt630a (16×22 1bpp RAM), panel (iPixel 8×16 1bpp)")
    args = ap.parse_args()

    if args.target == "amt630a":
        generate_amt630a(args)
        return
    if args.target == "panel":
        generate_panel(args)
        return

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
