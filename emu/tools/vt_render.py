# render raw terminal output (ANSI/VT) to text + PNG with pyte + PIL
import sys, pyte
from PIL import Image, ImageDraw, ImageFont
src, cols, rows = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
png = sys.argv[4] if len(sys.argv) > 4 else None
data = open(src, 'rb').read()
screen = pyte.Screen(cols, rows); stream = pyte.ByteStream(screen)
stream.feed(data)
lines = screen.display
for i, l in enumerate(lines): print('%2d|%s|' % (i, l))
if png:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 15)
    cw, ch = 9, 19
    img = Image.new('RGB', (cols * cw + 20, rows * ch + 20), (18, 18, 24)); d = ImageDraw.Draw(img)
    names = {'black': (0,0,0), 'red': (220,80,80), 'green': (90,200,110), 'brown': (210,180,70), 'blue': (90,130,230), 'magenta': (200,110,210), 'cyan': (80,200,210), 'white': (225,225,225), 'default': None}
    def col(c, dflt):
        if c in names and names[c]: return names[c]
        if c == 'default': return dflt
        try: return tuple(int(c[i:i+2], 16) for i in (0, 2, 4))
        except Exception: return dflt
    for y in range(rows):
        row = screen.buffer[y]
        for x in range(cols):
            ch_ = row[x]
            fg = col(ch_.fg, (225,225,225)); bg = col(ch_.bg, None)
            if ch_.reverse: fg, bg = (bg or (18,18,24)), fg
            if bg: d.rectangle([10 + x*cw, 10 + y*ch, 10 + (x+1)*cw, 10 + (y+1)*ch], fill=bg)
            if ch_.data and ch_.data != ' ': d.text((10 + x*cw, 10 + y*ch), ch_.data, font=font, fill=fg)
    img.save(png); print('saved', png)
