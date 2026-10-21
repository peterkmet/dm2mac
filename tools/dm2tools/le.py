"""Loader for LE (Linear Executable) programs bound to a DOS/4GW stub.

The loader places each object at its relocation base address, applies all
internal fixups, and returns one flat memory image. It supports only the
fixups that SKULL.EXE uses: internal 32-bit offsets (source type 0x07).
Other fixups raise ValueError.
"""

import struct
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Object:
    base: int
    size: int
    flags: int


@dataclass
class Image:
    data: bytes
    base: int
    objects: list[Object]
    entry: int


def load_image(path) -> Image:
    raw = Path(path).read_bytes()
    if raw[:2] != b"MZ":
        raise ValueError("no MZ header")
    le = struct.unpack_from("<I", raw, 0x3C)[0]
    if raw[le : le + 2] != b"LE":
        raise ValueError(f"no LE header at {le:#x}: {raw[le : le + 2]!r}")

    def hdr(off):
        return struct.unpack_from("<I", raw, le + off)[0]

    page_size, last_page_size = hdr(0x28), hdr(0x2C)
    num_pages, data_pages = hdr(0x14), hdr(0x80)
    page_map, fixup_pages, fixup_records = le + hdr(0x48), le + hdr(0x68), le + hdr(0x6C)

    objects, first_pages = [], []
    for i in range(hdr(0x44)):
        size, base, flags, first, count, _ = struct.unpack_from("<6I", raw, le + hdr(0x40) + 24 * i)
        objects.append(Object(base, size, flags))
        first_pages.append((first, count))

    lo = min(o.base for o in objects)
    hi = max(o.base + max(o.size, count * page_size) for o, (_, count) in zip(objects, first_pages))
    mem = bytearray(hi - lo)

    # Copy pages; page_addr[n] is the linear address of 1-based logical page n.
    page_addr = {}
    for obj, (first, count) in zip(objects, first_pages):
        for k in range(count):
            n = first + k
            entry = raw[page_map + 4 * (n - 1) : page_map + 4 * n]
            phys = (entry[0] << 16) | (entry[1] << 8) | entry[2]
            if entry[3] != 0:
                raise ValueError(f"page {n}: unsupported page type {entry[3]:#x}")
            length = last_page_size if phys == num_pages else page_size
            src = data_pages + (phys - 1) * page_size
            addr = obj.base + k * page_size
            mem[addr - lo : addr - lo + length] = raw[src : src + length]
            page_addr[n] = addr

    for n in range(1, num_pages + 1):
        start, end = struct.unpack_from("<2I", raw, fixup_pages + 4 * (n - 1))
        pos, end = fixup_records + start, fixup_records + end
        while pos < end:
            src, flags = raw[pos], raw[pos + 1]
            # Only internal 32-bit offset fixups with an 8-bit object number.
            if src != 0x07 or flags not in (0x00, 0x10):
                raise ValueError(f"page {n}: unsupported fixup src={src:#x} flags={flags:#x}")
            off, target_obj = struct.unpack_from("<hB", raw, pos + 2)
            if flags & 0x10:
                target_off = struct.unpack_from("<I", raw, pos + 5)[0]
                pos += 9
            else:
                target_off = struct.unpack_from("<H", raw, pos + 5)[0]
                pos += 7
            target = objects[target_obj - 1].base + target_off
            struct.pack_into("<I", mem, page_addr[n] + off - lo, target)

    entry = objects[hdr(0x18) - 1].base + hdr(0x1C)
    return Image(bytes(mem), lo, objects, entry)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: dm2-le SKULL.EXE OUT.bin")
    image = load_image(sys.argv[1])
    out = Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image.data)
    for i, obj in enumerate(image.objects, 1):
        print(f"object {i}: base {obj.base:#010x} size {obj.size:#x} flags {obj.flags:#x}")
    code_obj = next(i for i, o in enumerate(image.objects, 1) if o.base <= image.entry < o.base + o.size)
    print(f"entry: {code_obj:04x}:{image.entry:08x}")
