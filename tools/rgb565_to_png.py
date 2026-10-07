#!/usr/bin/env python3
"""Převede surový RGB565 480x480 (little endian) z host testů na kruhové PNG.

Použití: tools/rgb565_to_png.py vstup.rgb565 vystup.png
"""

from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

WIDTH = 480
HEIGHT = 480


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = zlib.crc32(payload, zlib.crc32(kind))
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


def convert(framebuffer: bytes, output_path: Path) -> None:
    if len(framebuffer) != WIDTH * HEIGHT * 2:
        raise SystemExit(f"Čekám {WIDTH * HEIGHT * 2} bajtů, přišlo {len(framebuffer)}.")
    rows = bytearray()
    center = (WIDTH - 1) / 2
    radius_squared = (WIDTH / 2) ** 2
    for y in range(HEIGHT):
        rows.append(0)
        dy_squared = (y - center) ** 2
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 2
            value = framebuffer[offset] | (framebuffer[offset + 1] << 8)
            red = ((value >> 11) & 0x1F) * 255 // 31
            green = ((value >> 5) & 0x3F) * 255 // 63
            blue = (value & 0x1F) * 255 // 31
            alpha = 255 if (x - center) ** 2 + dy_squared <= radius_squared else 0
            rows.extend((red, green, blue, alpha))
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)))
    png.extend(png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9)))
    png.extend(png_chunk(b"IEND", b""))
    output_path.write_bytes(png)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    convert(Path(sys.argv[1]).read_bytes(), Path(sys.argv[2]))
