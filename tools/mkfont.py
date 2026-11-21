# Generates font8x16.h from GNU Unifont (SIL OFL 1.1 / GPL w/ font exception)
import sys
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont
SRC='/usr/share/fonts/opentype/unifont/unifont.otf'
tt=TTFont(SRC); cmap=tt.getBestCmap(); hmtx=tt['hmtx']
font=ImageFont.truetype(SRC,16)
ranges=[(0x20,0x7E),(0xA0,0x24F),(0x2B0,0x2FF),(0x300,0x36F),(0x370,0x3FF),(0x400,0x4FF),(0x590,0x5FF),
        (0x1E00,0x1EFF),(0x1F00,0x1FFF),(0x2000,0x206F),(0x2070,0x209F),(0x20A0,0x20BF),(0x2100,0x214F),(0x2150,0x218F),
        (0x2190,0x21FF),(0x2200,0x22FF),(0x2300,0x23FF),(0x2500,0x257F),(0x2580,0x259F),(0x25A0,0x25FF),(0x2600,0x26FF),(0x2700,0x27BF),(0xFB1D,0xFB4F)]
out=[]
for a,b in ranges:
    for cp in range(a,b+1):
        if cp not in cmap: continue
        if hmtx[cmap[cp]][0]!=32: continue   # only 8-px-wide glyphs
        im=Image.new('1',(8,16),0); d=ImageDraw.Draw(im)
        d.text((0,14),chr(cp),font=font,fill=1,anchor='ls')
        rows=[]
        for y in range(16):
            v=0
            for x in range(8):
                if im.getpixel((x,y)): v|=0x80>>x
            rows.append(v)
        out.append((cp,rows))
with open(sys.argv[1],'w') as f:
    f.write('/* 8x16 glyphs rasterized from GNU Unifont (SIL Open Font License 1.1). */\n')
    f.write('#define FONT_GLYPHS %d\n'%len(out))
    f.write('static const unsigned short font_cp[FONT_GLYPHS]={\n')
    for i in range(0,len(out),16): f.write(','.join('0x%X'%c for c,_ in out[i:i+16])+',\n')
    f.write('};\nstatic const unsigned char font_bits[FONT_GLYPHS][16]={\n')
    for c,r in out: f.write('{'+','.join('%d'%v for v in r)+'},\n')
    f.write('};\n')
print(len(out))
# preview
for cp in [ord('A'),ord('g'),0x3B1,0x2554,0x1F00,0x5D0]:
    r=[x for c,x in out if c==cp]
    if r:
        print(hex(cp)); print('\n'.join(format(v,'08b').replace('0','.').replace('1','#') for v in r[0]))
