"""Makes the GameCube build's data from your copy of the game: its pictures
at 640x480 (the size the original draws at on a 640x480 screen, 0.625 of
its 1024x768 art), each already in a GameCube texture layout, so the
console only loads them.

    python tools/make_data.py

Only from Bejeweled 2 Deluxe installed through Steam (app 3300): the game is
found from Steam's own records (its libraries, and the app's manifest), and
some of its files must be the ones Steam installs, so the build uses the
owner's copy.

Writes BJ2GC/Scripts/Data/ (not committed: the art is PopCap's). A texture
file (.tex) is a 32-byte header, big-endian:

    "BJTX", u16 width, u16 height, u16 GX format (4 RGB565, 5 RGB5A3, 6 RGBA8),
    u16 frames across, u16 frames down, u16 frame width, u16 frame height

then the texels in the format's 4x4 tiles. An animation's frames are laid
out in a grid in one texture.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parents[1]
STEAM_APP = 3300  # Bejeweled 2 Deluxe
# Files of the Steam copy, by their SHA-256.
KNOWN_FILES = {
    'images/gem0.gif': 'd6fc4aa81fc84ab68a40b20a9ab1a5f08109ee834a2670095379a8f866b6f014',
    'images/sm_frame.gif': '633e988004c92df724794177d7b749c2b6d5261c15f54a7b1cadb054d477c2e2',
    'images/backdrops/backdrop00.jpg': '63ada7f5bde6513943db7c203d284bdc6ed7859916b5e4c20e59e866fad1800e',
    'music/BeyondNetwork.mo3': '3bb5742fcb17ad72892572f210560b5f95be487fa56a52dfd1f1cd355b936d82',
    'sounds/Level_Complete.ogg': 'a043c2441dbf6226ccb21fda431d40b938ac51e2341c3c4e4f13bc4ffb5d65fe',
    'data/QuincyCaps74gold2.txt': '21f712a6ea561efcd76417b6cf1d38a8796c98451494c6e24fc5679ab38165f9',
    'properties/resources.xml': '890c3b566f673f9a7304a2441a1a48b45dcf321956ff93bd0869af20a0443e59',
}


def steam_game():
    """The game's folder, from Steam: Steam's path (the registry), its
    libraries (steamapps/libraryfolders.vdf), the library holding the app's
    manifest (appmanifest_3300.acf) and the folder that names; then its
    files checked. Exits, saying why, if any of it isn't there."""
    import hashlib
    import re
    steam = None
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
            steam = Path(winreg.QueryValueEx(key, 'SteamPath')[0])
    except OSError:
        pass
    if steam is None or not steam.exists():
        steam = Path(r'C:\Program Files (x86)\Steam')
    libraries = [steam]
    vdf = steam / 'steamapps' / 'libraryfolders.vdf'
    if vdf.exists():
        for path in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding='utf-8', errors='replace')):
            libraries.append(Path(path.replace('\\\\', '\\')))  # the file doubles its backslashes
    for library in libraries:
        manifest = library / 'steamapps' / f'appmanifest_{STEAM_APP}.acf'
        if not manifest.exists():
            continue
        found = re.search(r'"installdir"\s+"([^"]+)"', manifest.read_text(encoding='utf-8', errors='replace'))
        game = library / 'steamapps' / 'common' / found.group(1) if found else None
        if game is None or not game.is_dir():
            sys.exit(f'Steam lists Bejeweled 2 Deluxe ({manifest}) but its folder is missing: install it in Steam.')
        for name, digest in KNOWN_FILES.items():
            file = game / name
            if not file.exists() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
                sys.exit(f"{file} is missing or not the Steam copy's: verify the game's files in Steam.")
        return game
    sys.exit('Bejeweled 2 Deluxe (Steam app 3300) is not installed through Steam on this PC: '
             'the data is made only from your own Steam copy.')


GAME = None  # the game's folder, from Steam (main)
OUT = HERE / 'BJ2GC' / 'Scripts' / 'Data'
SCALE = 0.625
RGB565, RGB5A3, RGBA8 = 4, 5, 6
CELL = 52  # a gem at 640x480 (84 x 0.625 = 52.5)


def load(name, mask=None):
    """An image with its PopCap alpha mask (NAME_.gif: white opaque)."""
    images = GAME / 'images'
    im = Image.open(images / name).convert('RGB')
    if mask is None:
        stem = Path(name).stem
        for ext in ('.gif', '.jpg', '.png'):
            if (images / (stem + '_' + ext)).exists():
                mask = stem + '_' + ext
                break
    if mask:
        a = Image.open(images / mask).convert('L')
        im = im.convert('RGBA')
        im.putalpha(a)
    return im


def texels(im, fmt):
    """The texels of `im` in 4x4 tiles, 16 bits each, big-endian (RGBA8: a
    tile's alpha and red pairs, then its green and blue pairs)."""
    w, h = im.size
    px = im.convert('RGBA').load()
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            if fmt == RGBA8:
                tile = [px[x, y] if x < w and y < h else (0, 0, 0, 0)
                        for y in range(ty, ty + 4) for x in range(tx, tx + 4)]
                out += bytes(v for r, g, b, a in tile for v in (a, r))
                out += bytes(v for r, g, b, a in tile for v in (g, b))
                continue
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, a = px[x, y] if x < w and y < h else (0, 0, 0, 0)
                    if fmt == RGB565:
                        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                    elif a >= 0xE0:
                        v = 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
                    else:
                        v = ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
                    out += struct.pack('>H', v)
    return out


def write(name, im, fmt, frames=(1, 1), frame=None):
    w, h = im.size
    pw, ph = (w + 3) // 4 * 4, (h + 3) // 4 * 4
    if (pw, ph) != (w, h):
        canvas = Image.new('RGBA', (pw, ph), (0, 0, 0, 0))
        canvas.paste(im.convert('RGBA'), (0, 0))
        im = canvas
    fw, fh = frame or (w, h)
    header = b'BJTX' + struct.pack('>8H', pw, ph, fmt, frames[0], frames[1], fw, fh, 0)
    header += bytes(32 - len(header))
    data = header + texels(im, fmt)
    (OUT / name).write_bytes(data)
    print(f'{name}: {pw}x{ph} {({RGB565: "RGB565", RGB5A3: "RGB5A3", RGBA8: "RGBA8"})[fmt]}, {len(data) // 1024} KB')


def strip_to_grid(strip, count, per_row, size=CELL, row=0, frame_src=84):
    """Frames of a horizontal strip (frame_src wide), shrunk to `size` and
    laid out `per_row` across."""
    rows = (count + per_row - 1) // per_row
    grid = Image.new('RGBA', (per_row * size, rows * size), (0, 0, 0, 0))
    for i in range(count):
        f = strip.crop((i * frame_src, row * frame_src, (i + 1) * frame_src, (row + 1) * frame_src))
        f = f.resize((size, size), Image.LANCZOS)
        grid.paste(f, ((i % per_row) * size, (i // per_row) * size))
    return grid, (per_row, rows)


def parse_font(path):
    """A PopCap font description (data/*.txt): its one layer's image name,
    ascent and glyphs {char: (advance, (x, y, w, h), (ox, oy))}."""
    import re
    text = path.read_text(encoding='latin-1')
    tokens = re.findall(r"'(?:[^']|'')'|\"[^\"]*\"|-?\d+|[A-Za-z_]\w*|[();]", text)
    pos = 0

    def value():
        nonlocal pos
        t = tokens[pos]
        pos += 1
        if t == '(':
            items = []
            while tokens[pos] != ')':
                items.append(value())
            pos += 1
            return items
        if t[0] in "'\"":
            return t[1:-1]
        if t.lstrip('-').isdigit():
            return int(t)
        return defines.get(t, t)

    defines, image, ascent = {}, None, 0
    widths, rects, offsets = {}, {}, {}
    while pos < len(tokens):
        word = tokens[pos]
        pos += 1
        if word == 'Define':
            name = tokens[pos]
            pos += 1
            defines[name] = value()
        elif word == 'LayerSetImage':
            pos += 1
            image = value()
        elif word == 'LayerSetAscent':
            pos += 1
            ascent = value()
        elif word in ('LayerSetCharWidths', 'LayerSetImageMap', 'LayerSetCharOffsets'):
            pos += 1
            chars, vals = value(), value()
            table = {'LayerSetCharWidths': widths, 'LayerSetImageMap': rects, 'LayerSetCharOffsets': offsets}[word]
            for c, v in zip(chars, vals):
                table[c] = v
    glyphs = {c: (widths.get(c, 0), rects.get(c, (0, 0, 0, 0)), offsets.get(c, (0, 0))) for c in widths}
    return image, ascent, glyphs


def write_font(name, desc, scale=1.0):
    """A font: its glyphs repacked into a texture at most 512 wide (NAME.tex)
    and their table (NAME.fnt): "BJFN", u16 ascent, u16 count, then each
    glyph u16 char, i16 advance, u16 x, y, w, h (in the texture), i16 ox, oy.
    `scale` shrinks it here, smoothly, so the console draws it at 1."""
    image, ascent, glyphs = parse_font(GAME / 'data' / desc)
    data = GAME / 'data'

    # The colour layer is NAME, its alpha _NAME or NAME_ (PopCap's names).
    def find(*names):
        return next((data / (n + e) for n in names for e in ('.gif', '.png') if (data / (n + e)).exists()), None)
    colour, alpha = find(image), find('_' + image, image + '_')
    base = Image.open(colour or alpha).convert('RGB')
    if colour is None:
        base = Image.new('RGB', base.size, (255, 255, 255))  # white text, its shape the alpha
    sheet = base.convert('RGBA')
    if alpha:
        sheet.putalpha(Image.open(alpha).convert('L'))
    # Each glyph, scaled, with a pixel of clear border so its edge filters to nothing.
    cells = {}
    for c, (adv, (rx, ry, rw, rh), (ox, oy)) in glyphs.items():
        im = None
        if rw and rh:
            im = sheet.crop((rx, ry, rx + rw, ry + rh))
            if scale != 1.0:
                im = im.resize((max(1, round(rw * scale)), max(1, round(rh * scale))), Image.LANCZOS)
            framed = Image.new('RGBA', (im.width + 2, im.height + 2), (0, 0, 0, 0))
            framed.paste(im, (1, 1))
            im = framed
        cells[c] = (round(adv * scale), im, round(ox * scale) - 1, round(oy * scale) - 1)
    # Shelf packing, in the order of the characters.
    x = y = shelf = 0
    placed = {}
    for c, (adv, im, ox, oy) in cells.items():
        w, h = (im.size if im else (0, 0))
        if x + w > 512:
            x, y, shelf = 0, y + shelf, 0
        placed[c] = (x, y)
        x += w
        shelf = max(shelf, h)
    atlas = Image.new('RGBA', (512, y + shelf), (0, 0, 0, 0))
    table = b''
    for c, (adv, im, ox, oy) in cells.items():
        px, py = placed[c]
        w, h = (im.size if im else (0, 0))
        if im:
            atlas.paste(im, (px, py))
        table += struct.pack('>HhHHHHhh', ord(c), adv, px, py, w, h, ox, oy)
    (OUT / (name + '.fnt')).write_bytes(b'BJFN' + struct.pack('>HH', round(ascent * scale), len(cells)) + table)
    write(name + '.tex', atlas, RGBA8)


# The ffmpeg that comes with Octave-libogc (next to this repo, or $OCTAVE).
import os
OCTAVE = Path(os.environ.get('OCTAVE') or HERE.parent / 'octave-libogc')
FFMPEG = OCTAVE / 'External' / 'ffmpeg' / 'bin' / 'ffmpeg.exe'
SOUNDS = ['select', 'bad2', 'gotset2', 'gotsetbig2', 'combo22', 'combo32', 'combo42', 'combo52', 'combo62',
          'combo72', 'gemongem2', 'explode2', 'hypergem_creation', 'electro_start', 'electro_explode',
          'Level_Complete', 'Go', 'No_More_Moves', 'excellent1', 'Incredible', 'Get_ready', 'Good', 'multishot', 'whirlpool1', 'electro_path']
SOUND_RATE = 32000


def write_sounds():
    """The effects the game plays, as 16-bit mono PCM (snd_NAME.pcm): "BJSD",
    u32 rate, u32 bytes, padding to 32, then the samples, big-endian, padded
    to 32 bytes (as ASND plays them)."""
    import subprocess
    for name in SOUNDS:
        pcm = subprocess.run([str(FFMPEG), '-v', 'error', '-i', str(GAME / 'sounds' / (name + '.ogg')), '-ac', '1',
                              '-ar', str(SOUND_RATE), '-f', 's16be', '-'], capture_output=True, check=True).stdout
        pcm += bytes(-len(pcm) % 32)
        header = b'BJSD' + struct.pack('>II', SOUND_RATE, len(pcm))
        (OUT / f'snd_{name.lower()}.pcm').write_bytes(header + bytes(32 - len(header)) + pcm)
    print(f'{len(SOUNDS)} sounds')


# The music: Beyond the Network (music/BeyondNetwork.mo3), one tracker module
# the game plays from different orders (properties/music.xml). Classic plays
# MUSICOFFSET_MAIN, order 2, which libopenmpt finds as subsong 1: 31 minutes
# that loop. Streamed from the disc, so Ogg Vorbis.
MUSIC_RATE = 32000


def write_music():
    """music_main.ogm: "BJMU", u32 Ogg bytes, padding to 32, then the Ogg."""
    import subprocess
    ogg = subprocess.run([str(FFMPEG), '-v', 'error', '-subsong', '1', '-i', str(GAME / 'music' / 'BeyondNetwork.mo3'),
                          '-ac', '2', '-ar', str(MUSIC_RATE), '-c:a', 'libvorbis', '-q:a', '2', '-f', 'ogg', '-'],
                         capture_output=True, check=True).stdout
    header = b'BJMU' + struct.pack('>I', len(ogg))
    (OUT / 'music_main.ogm').write_bytes(header + bytes(32 - len(header)) + ogg)
    print(f'music_main.ogm: {len(ogg) // 1024} KB')


def write_effects():
    """The board's effects (docs/board-effects.md), at 0.625 of the original's
    art: added ones as RGB565 (their black adds nothing), the rest with alpha."""
    images = GAME / 'images'
    grey = lambda name: Image.open(images / name).convert('RGB')
    grid, frames = strip_to_grid(grey('explosion.jpg'), 20, 5, size=50, frame_src=80)
    write('explosion.tex', grid, RGB565, frames, (50, 50))
    grid, frames = strip_to_grid(load('gemshard.gif'), 40, 8, size=19, frame_src=30)
    write('gemshard.tex', grid, RGB5A3, frames, (19, 19))
    grid, frames = strip_to_grid(grey('sparkle.gif'), 14, 7, size=25, frame_src=40)
    write('sparkle.tex', grid, RGB565, frames, (25, 25))
    write('bigstar.tex', grey('bigstar.gif').resize((76, 76), Image.LANCZOS), RGB565)
    grid, frames = strip_to_grid(grey('powerglow.jpg'), 10, 5, size=128, frame_src=240)
    write('powerglow.tex', grid, RGB565, frames, (128, 128))
    write('lightning.tex', grey('lightning.png'), RGB565)
    write('lightning_center.tex', grey('lightning_center.png'), RGB565)
    write('hint_arrow.tex', load('hint_arrow.gif').resize((64, 50), Image.LANCZOS), RGB5A3)
    # The gems' lighting (al_litgems.gif): a row a colour, a column a facet
    # (up, up-left, ... up-right, then the middle), added over the gem.
    lit = grey('al_litgems.gif')
    grid = Image.new('RGB', (9 * CELL, 7 * CELL))
    for row in range(7):
        for col in range(9):
            cell = lit.crop((col * 84, row * 84, col * 84 + 84, row * 84 + 84)).resize((CELL, CELL), Image.LANCZOS)
            grid.paste(cell, (col * CELL, row * CELL))
    write('litgems.tex', grid, RGB565, (9, 7), (CELL, CELL))
    arrows = Image.new('RGBA', (40, 40), (255, 255, 255, 0))
    arrows.putalpha(Image.open(images / 'help_indicator_arrows_.gif').convert('L').crop((0, 0, 40, 40)))
    write('hint_glow.tex', arrows.resize((24, 24), Image.LANCZOS), RGB5A3)


def main():
    global GAME
    GAME = steam_game()
    print(f'Bejeweled 2 Deluxe (Steam): {GAME}')
    OUT.mkdir(parents=True, exist_ok=True)
    write_music()
    write_sounds()
    write_font('font_score', 'ContinuumBold27score.txt')
    write_font('font_small', 'ContinuumMedium18outline.txt', 0.7)
    write_font('font_label', 'Halfmoon30.txt', 0.8)
    write_font('font_big', 'QuincyCaps74gold2.txt')
    write_font('font_points', 'ContinuumBold60outline.txt', SCALE)
    for i in range(10):
        im = Image.open(GAME / 'images' / 'backdrops' / f'backdrop{i:02}.jpg').convert('RGB')
        write(f'backdrop{i:02}.tex', im.resize((640, 480), Image.LANCZOS), RGB565)
    for c in range(7):
        grid, frames = strip_to_grid(load(f'gem{c}.gif'), 20, 5)
        write(f'gem{c}.tex', grid, RGB5A3, frames, (CELL, CELL))
    grid, frames = strip_to_grid(load('hypergem.jpg'), 40, 8)
    write('hypergem.tex', grid, RGB5A3, frames, (CELL, CELL))
    # The power gems' glow, drawn added over them: a row of 20 frames a colour.
    glow = Image.open(GAME / 'images' / 'gem_add.jpg').convert('RGBA')
    for c in range(7):
        grid, frames = strip_to_grid(glow, 20, 5, row=c)
        write(f'glow{c}.tex', grid, RGB565, frames, (CELL, CELL))
    write('selector.tex', load('selector.gif').resize((CELL, CELL), Image.LANCZOS), RGB5A3)
    write('frame.tex', load('sm_frame.gif'), RGB5A3)
    # The level bar's glow (IMAGE_BAR_LEFT/MID/RIGHT): grey on black, drawn added.
    for part in ('left', 'mid', 'right'):
        im = Image.open(GAME / 'images' / f'bar{part}.gif').convert('RGB')
        write(f'bar{part}.tex', im.resize((round(im.width * SCALE), round(im.height * SCALE)), Image.LANCZOS), RGB565)
    # The level transition (docs/level-transition.md): the hyperspace tunnel's textures,
    # which tile (so a power of two), the tunnel's end and fire ring, the
    # black hole and its cover.
    images = GAME / 'images'
    for name, size in (('nr_hyperspace', 512), ('nr_hyperspace_initial', 256), ('nr_warplines', 512)):
        im = Image.open(images / f'{name}.jpg').convert('RGB')
        write(f'{name[3:]}.tex', im.resize((size, size), Image.LANCZOS), RGB565)
    write('tunnelend.tex', load('tunnelend.png'), RGB5A3)
    grid, frames = strip_to_grid(Image.open(images / 'firering.jpg').convert('RGB'), 10, 5, size=128, frame_src=200)
    write('firering.tex', grid, RGB565, frames, (128, 128))
    grid, frames = strip_to_grid(Image.open(images / 'blackhole_chopped.jpg').convert('RGB'), 5, 5, size=160,
                                 frame_src=256)
    write('blackhole.tex', grid, RGB565, frames, (160, 160))
    write('holemask.tex', Image.open(images / 'holemask.png').convert('RGBA').resize((80, 80), Image.LANCZOS), RGB5A3)
    write_effects()
    pod = load('SCOREPOD.jpg', 'scorePod_.gif')
    write('scorepod.tex', pod.resize((round(pod.width * SCALE), round(pod.height * SCALE)), Image.LANCZOS), RGB5A3)
    print(f'-> {OUT}')


if __name__ == '__main__':
    main()
