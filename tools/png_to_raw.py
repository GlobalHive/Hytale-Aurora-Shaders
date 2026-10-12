# Converts a PNG into the hook's raw texture format: "HYTX" + u32 width + u32 height + RGBA8.
# usage: python png_to_raw.py <in.png> <out.raw>
import struct, sys
from PIL import Image

im = Image.open(sys.argv[1]).convert('RGBA')
w, h = im.size
with open(sys.argv[2], 'wb') as f:
    f.write(b'HYTX' + struct.pack('<II', w, h) + im.tobytes())
print(sys.argv[2], w, h)
