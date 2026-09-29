"""The disc's banner, from art/banner.png (96 x 32).

    python tools/make_banner.py        (needs Pillow)

Writes BJ2GC/opening.bnr, the banner Dolphin, Swiss and the console's menu
show; the packager puts a project's own opening.bnr on the disc in place of
the engine's default. BNR1: "BNR1", padding to 0x20; the picture at 0x20,
96 x 32 RGB5A3 in 4 x 4 tiles, big-endian (0x1800 bytes); then the short
name (0x20), short maker (0x20), long name (0x40), long maker (0x40) and
description (0x80). 0x1960 bytes. (As PPGC's tools/make_art.py.)
"""
import struct
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parents[1]

NAME = 'Bejeweled 2'
MAKER = 'Octave Engine'  # as the engine's default banner
DESCRIPTION = "Bejeweled 2's Classic mode for the GameCube"


def rgb5a3(r, g, b, a=255):
    if a >= 0xE0:  # opaque: 1 RRRRR GGGGG BBBBB
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)  # 0 AAA RRRR GGGG BBBB


def text(value, size):
    data = value.encode('ascii')[:size - 1]
    return data + b'\0' * (size - len(data))


def main():
    img = Image.open(HERE / 'art' / 'banner.png').convert('RGBA')
    if img.size != (96, 32):
        img = img.resize((96, 32), Image.LANCZOS)
    px = img.load()
    tiles = bytearray()
    for ty in range(0, 32, 4):
        for tx in range(0, 96, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    tiles += struct.pack('>H', rgb5a3(*px[x, y]))
    bnr = bytearray(b'BNR1' + b'\0' * 0x1C) + tiles
    bnr += text(NAME, 0x20) + text(MAKER, 0x20) + text(NAME, 0x40) + text(MAKER, 0x40) + text(DESCRIPTION, 0x80)
    assert len(bnr) == 0x1960, hex(len(bnr))
    (HERE / 'BJ2GC' / 'opening.bnr').write_bytes(bytes(bnr))
    print(f'wrote {HERE / "BJ2GC" / "opening.bnr"}')


if __name__ == '__main__':
    main()
