"""Generates the stock dialog box art: engine/assets/ui/dialog/*.png, and
voice.wav, the blip a speaker without a voice of their own talks in.

Placeholder pixel art until an artist draws the real thing - a game replaces
these files (or points DialogScreen at its own). Small images, drawn at 3x
with a nearest sampler, so they stay crisp. Plain Python, no packages:

    python tools/art/dialog_art.py
"""

import math
import os
import struct
import wave
import zlib

OUT = os.path.join(os.path.dirname(__file__), '..', '..', 'engine', 'assets', 'ui', 'dialog')

CLEAR = (0, 0, 0, 0)
NAVY = (38, 48, 80, 255)
NAVY_LIGHT = (92, 108, 150, 255)
GOLD = (216, 176, 96, 255)
GOLD_DARK = (160, 120, 56, 255)
CREAM = (251, 246, 233, 255)
CREAM_SHADE = (232, 222, 196, 255)


def write_png(path, pixels):
    """pixels: rows of (r, g, b, a)."""
    height, width = len(pixels), len(pixels[0])
    raw = b''.join(b'\x00' + bytes(c for px in row for c in px) for row in pixels)

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def frame(size, rings, fill, corner):
    """A square nine-slice frame: rings of colors from the edge in, then fill.
    corner: how many pixels each corner is cut; the rings follow the cut."""
    img = [[fill] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            cx, cy = min(x, size - 1 - x), min(y, size - 1 - y)
            if cx + cy < corner:
                img[y][x] = CLEAR
                continue
            ring = min(cx, cy, cx + cy - corner)  # distance from the edge, the cut included
            if ring < len(rings):
                img[y][x] = rings[ring]
    return img


def triangle(width, height, color, edge, pointing):
    """A filled triangle with a one-pixel edge, pointing 'down' or 'right'."""
    img = [[CLEAR] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            if pointing == 'down':
                half = (width / 2) * (1 - y / (height - 0.5))
                inside = abs(x + 0.5 - width / 2) <= half
                border = inside and (abs(x + 0.5 - width / 2) > half - 1.2 or y == 0)
            else:
                half = (height / 2) * (1 - x / (width - 0.5))
                inside = abs(y + 0.5 - height / 2) <= half
                border = inside and (abs(y + 0.5 - height / 2) > half - 1.2 or x == 0)
            if inside:
                img[y][x] = edge if border else color
    return img


def write_blip(path):
    """A short soft square-ish blip, 45 ms, falling off fast."""
    rate, length, pitch = 22050, 0.045, 540.0
    frames = bytearray()
    for i in range(int(rate * length)):
        t = i / rate
        tone = math.tanh(4.0 * math.sin(2 * math.pi * pitch * t))  # rounded square
        envelope = min(1.0, t / 0.004) * math.exp(-t / 0.018)
        frames += struct.pack('<h', int(tone * envelope * 0.3 * 32767))
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))


def main():
    os.makedirs(OUT, exist_ok=True)
    # The text box and the choice box: navy edge, gold trim, cream inside.
    box = frame(24, [NAVY, NAVY, GOLD, GOLD_DARK, CREAM_SHADE], CREAM, 2)
    write_png(os.path.join(OUT, 'box.png'), box)
    # The name plate: navy, gold edge.
    plate = frame(16, [GOLD_DARK, GOLD, NAVY_LIGHT], NAVY, 1)
    write_png(os.path.join(OUT, 'name_plate.png'), plate)
    write_png(os.path.join(OUT, 'next_arrow.png'), triangle(10, 7, GOLD, NAVY, 'down'))
    write_png(os.path.join(OUT, 'cursor.png'), triangle(7, 10, GOLD, NAVY, 'right'))
    write_blip(os.path.join(OUT, 'voice.wav'))
    print('wrote', os.path.normpath(OUT))


if __name__ == '__main__':
    main()
