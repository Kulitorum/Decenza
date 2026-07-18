import struct, re, sys, glob, os, unicodedata

def cmap_of(path):
    d=open(path,"rb").read(); n=struct.unpack(">H",d[4:6])[0]; co=None
    for i in range(n):
        r=12+i*16
        if d[r:r+4]==b"cmap": co=struct.unpack(">I",d[r+8:r+12])[0]; break
    m=struct.unpack(">H",d[co+2:co+4])[0]; out=set()
    for i in range(m):
        e=co+4+i*8; sub=co+struct.unpack(">I",d[e+4:e+8])[0]
        f=struct.unpack(">H",d[sub:sub+2])[0]
        if f==4:
            sx=struct.unpack(">H",d[sub+6:sub+8])[0]; sg=sx//2
            ends=[struct.unpack(">H",d[sub+14+j*2:sub+16+j*2])[0] for j in range(sg)]
            sp=sub+16+sx
            sts=[struct.unpack(">H",d[sp+j*2:sp+2+j*2])[0] for j in range(sg)]
            for s,e2 in zip(sts,ends):
                if s!=0xFFFF: out.update(range(s,min(e2,0xFFFF)+1))
        elif f==12:
            g=struct.unpack(">I",d[sub+12:sub+16])[0]
            for k in range(g):
                go=sub+16+k*12; s,e2,_=struct.unpack(">III",d[go:go+12])
                out.update(range(s,min(e2,s+5000)+1))
    return out

cmap = cmap_of("resources/fonts/DecenzaSans-Regular.ttf")

# Emoji are fine: they never reach the text renderer (rewritten to bundled <img>).
def is_emoji(cp):
    return (0x1F000 <= cp <= 0x1FAFF or 0x2600 <= cp <= 0x27BF
            or cp in (0xFE0F,0x200D,0x20E3,0xA9,0xAE) or 0x1F1E6 <= cp <= 0x1F1FF
            or 0x2B00 <= cp <= 0x2BFF)

STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
hits = {}
files = glob.glob("qml/**/*.qml", recursive=True)
for path in files:
    for ln, line in enumerate(open(path, encoding="utf-8"), 1):
        s = line.strip()
        if s.startswith("//") or s.startswith("*"): continue
        for m in STRING.finditer(line):
            for ch in m.group(1):
                cp = ord(ch)
                if cp < 0x80 or is_emoji(cp) or cp in cmap: continue
                hits.setdefault(ch, []).append(f"{path}:{ln}")

print(f"Scanned {len(files)} QML files against DecenzaSans cmap ({len(cmap)} glyphs)\n")
if not hits:
    print("No missing glyphs."); sys.exit()
for ch, locs in sorted(hits.items(), key=lambda kv: -len(kv[1])):
    try: nm = unicodedata.name(ch)
    except ValueError: nm = "?"
    print(f"  {ch}  U+{ord(ch):04X}  {nm:38} {len(locs):3} site(s)")
    for l in sorted(set(locs))[:4]: print(f"        {l}")
    if len(set(locs)) > 4: print(f"        … and {len(set(locs))-4} more")
