"""Bake the existing badge-dots bitmap subset into the small RGB565 overlay."""
from io import BytesIO
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[3]
source = TTFont(ROOT / "design/pixel-badge/assets/badge-dots.woff")
codes = sorted(code for code in source.getBestCmap() if 32 <= code <= 65535)
source.flavor = None
data = BytesIO()
source.save(data)
data.seek(0)
face = ImageFont.truetype(data, 12)
lines = ['/* Generated from badge-dots.woff. See authoring/bake_font.py and LICENSE.font. */',
         'static const struct { uint16_t code, width, rows[12]; } font[] = {']
for code in codes:
    ch = chr(code)
    width = round(face.getlength(ch))
    if width <= 0 or width > 12:
        continue
    tile = Image.new('1', (width, 12))
    ImageDraw.Draw(tile).text((0, 0), ch, font=face, fill=1)
    rows = [sum(1 << x for x in range(width) if tile.getpixel((x, y))) for y in range(12)]
    lines.append('    {%d,%d,{%s}},' % (code, width, ','.join(map(str, rows))))
lines.append('};')
(ROOT / "components/baji_agent_ui/badge_font.inc").write_text('\n'.join(lines) + '\n', encoding='ascii')
