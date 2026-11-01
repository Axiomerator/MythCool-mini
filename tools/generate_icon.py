"""Original cooler-screen/drop icon. Python standard library; no runtime dependency."""
# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import math, struct, zlib
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'assets'; ASSETS.mkdir(exist_ok=True)
SVG="""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
 <circle cx="128" cy="128" r="120" fill="#21c4d7"/>
 <circle cx="128" cy="128" r="103" fill="#172e40"/>
 <path d="M128 58 C119 79 86 105 86 128 A42 42 0 0 0 170 128 C170 105 137 79 128 58Z" fill="#f3fbff"/>
 <path d="M101 184h13v20h-13zm21-8h13v28h-13zm21 12h13v16h-13z" fill="#21c4d7"/>
</svg>
"""
(ASSETS/'app.svg').write_text(SVG,encoding='utf-8')

def width_at(y):
    if y<58 or y>170:return -1
    if y>=128:return math.sqrt(max(0,42*42-(y-128)**2))
    lo,hi=0.,1.
    for _ in range(30):
        t=(lo+hi)/2;u=1-t
        by=u*u*u*58+3*u*u*t*79+3*u*t*t*105+t*t*t*128
        if by<y:lo=t
        else:hi=t
    t=(lo+hi)/2;u=1-t
    bx=u*u*u*128+3*u*u*t*119+3*u*t*t*86+t*t*t*86
    return 128-bx

def raster(size):
    out=bytearray()
    for y in range(size):
        rows=[((y+(sy+.5)/4)*256/size) for sy in range(4)]
        halves=[width_at(py) for py in rows]
        for x in range(size):
            totals=[0,0,0];alpha=0
            for py,hw in zip(rows,halves):
                for sx in range(4):
                    px=(x+(sx+.5)/4)*256/size;d=(px-128)**2+(py-128)**2
                    if d>120**2:continue
                    color=(33,196,215) if d>103**2 else (23,46,64)
                    if hw>=0 and abs(px-128)<=hw:color=(243,251,255)
                    if (101<=px<114 and 184<=py<204) or (122<=px<135 and 176<=py<204) or (143<=px<156 and 188<=py<204):color=(33,196,215)
                    alpha+=255
                    for i in range(3):totals[i]+=color[i]
            count=alpha//255
            rgb=[round(n/count) for n in totals] if count else [0,0,0]
            out.extend(bytes(rgb+[round(alpha/16)]))
    return bytes(out)

def png(size,rgba):
    def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
    rows=b''.join(b'\0'+rgba[y*size*4:(y+1)*size*4] for y in range(size))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b'')
images=[]
for size in (16,20,24,32,48,64,128,256):
    rgba=raster(size)
    if size>=64:data=png(size,rgba)
    else:
        bgra=bytearray()
        for y in range(size-1,-1,-1):
            for x in range(size):
                p=(y*size+x)*4;r,g,b,a=rgba[p:p+4];bgra.extend((b,g,r,a))
        mask=bytes(((size+31)//32)*4*size)
        data=struct.pack('<IiiHHIIiiII',40,size,size*2,1,32,0,len(bgra),0,0,0,0)+bgra+mask
    images.append((size,data))
    if size==256:(ASSETS/'app-preview.png').write_bytes(png(size,rgba))
offset=6+16*len(images);directory=bytearray()
for size,data in images:
    directory.extend(struct.pack('<BBBBHHII',size if size<256 else 0,size if size<256 else 0,0,0,1,32,len(data),offset));offset+=len(data)
(ASSETS/'app.ico').write_bytes(struct.pack('<HHH',0,1,len(images))+directory+b''.join(data for _,data in images))
print('Generated original SVG, preview and 8-size ICO:',(ASSETS/'app.ico').stat().st_size,'bytes')
