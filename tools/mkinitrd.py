#!/usr/bin/env python3
"""
Build a KIRD initrd image.

Usage:
    mkinitrd.py output.img path/to/file:name_in_fs [...]

Each <path>:<name> argument adds the file at <path> to the archive
under the name <name>.

Format:
    4 bytes  magic  "KIRD" (0x4B495244 little-endian)
    4 bytes  count  number of files
    For each file:
        28 bytes  name   null-padded
         4 bytes  size   byte length
         N bytes  data
"""

import sys
import struct

MAGIC     = b'KIRD'
NAMELEN   = 28

def build(output, entries):
    with open(output, 'wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<I', len(entries)))
        for path, name in entries:
            data = open(path, 'rb').read()
            name_bytes = name.encode('utf-8')[:NAMELEN - 1]
            name_bytes = name_bytes + b'\x00' * (NAMELEN - len(name_bytes))
            f.write(name_bytes)
            f.write(struct.pack('<I', len(data)))
            f.write(data)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('Usage: mkinitrd.py output.img path:name [...]', file=sys.stderr)
        sys.exit(1)

    out = sys.argv[1]
    pairs = []
    for arg in sys.argv[2:]:
        if ':' not in arg:
            print(f'Bad argument (expected path:name): {arg}', file=sys.stderr)
            sys.exit(1)
        path, name = arg.split(':', 1)
        pairs.append((path, name))

    build(out, pairs)
    print(f'Created {out} with {len(pairs)} file(s).')
