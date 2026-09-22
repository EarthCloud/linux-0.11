import zlib, struct, sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
# --- parse P6 header ---
pos = 0
def token(data, pos):
    while True:
        while pos < len(data) and data[pos:pos+1].isspace(): pos += 1
        if data[pos:pos+1] == b'#':
            while data[pos:pos+1] not in (b'\n', b''): pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos+1].isspace(): pos += 1
        return data[start:pos], pos
magic, pos = token(data, pos)
w, pos = token(data, pos); h, pos = token(data, pos)
maxv, pos = token(data, pos)
pos += 1
w, h = int(w), int(h)
px = data[pos:pos + w*h*3]
assert len(px) == w*h*3, (len(px), w*h*3)
raw = b''.join(b'\x00' + px[y*w*3:(y+1)*w*3] for y in range(h))
def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
png = (b'\x89PNG\r\n\x1a\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(raw, 9))
       + chunk(b'IEND', b''))
open(dst, 'wb').write(png)
print(f'{w}x{h} -> {dst} ({len(png)} bytes)')