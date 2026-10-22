#!/usr/bin/env python3
"""Regenerate app icons from the SVG. Requires rsvg-convert; ICNS requires macOS."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

icons = Path(__file__).resolve().parents[1] / 'src/tray/icons'
source = icons / 'lyrion-player.svg'

with tempfile.TemporaryDirectory(prefix='lyrion-icons-') as temporary:
    output = Path(temporary)
    pngs = {}
    for size in (16, 24, 32, 48, 64, 128, 256, 512, 1024):
        path = output / f'{size}.png'
        subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size),
                        '-o', str(path), str(source)], check=True)
        pngs[size] = path
    shutil.copyfile(pngs[512], icons / 'lyrion-player.png')
    sizes = (16, 24, 32, 48, 64, 128, 256)
    offset = 6 + 16 * len(sizes)
    entries, images = [], []
    for size in sizes:
        data = pngs[size].read_bytes()
        entries.append(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0,
                                   1, 32, len(data), offset))
        images.append(data)
        offset += len(data)
    (icons / 'lyrion-player.ico').write_bytes(
        struct.pack('<HHH', 0, 1, len(sizes)) + b''.join(entries + images))
    if shutil.which('iconutil'):
        iconset = output / 'lyrion-player.iconset'
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            shutil.copyfile(pngs[size], iconset / f'icon_{size}x{size}.png')
            shutil.copyfile(pngs[size * 2], iconset / f'icon_{size}x{size}@2x.png')
        subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o',
                        str(icons / 'lyrion-player.icns')], check=True)
    else:
        print('ICNS regeneration requires iconutil on macOS.')
