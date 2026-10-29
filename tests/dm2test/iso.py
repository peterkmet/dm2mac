# SPDX-License-Identifier: GPL-3.0-or-later
"""Small ISO 9660 CD images (ECMA-119) for the tests of the CD image reader."""

SECTOR = 2048
RAW_SECTOR = 2352
RAW_SYNC = bytes([0x00] + [0xFF] * 10 + [0x00])
RAW_TAIL = 288  # the EDC and ECC bytes of a MODE1 sector; zero, the reader does not check them
SYSTEM_AREA = 16  # the empty sectors before the primary volume descriptor
ROOT_SECTOR = SYSTEM_AREA + 2  # after the primary volume descriptor and the terminator
FOLDER_FLAG = 0x02
LEAD_IN = 150  # the address of sector 0 in frames
FRAMES, SECONDS = 75, 60


def _both16(value):
    return value.to_bytes(2, "little") + value.to_bytes(2, "big")


def _both32(value):
    return value.to_bytes(4, "little") + value.to_bytes(4, "big")


def _record(name, sector, size, folder):
    """A directory record: the extent, the data length, the flags and the name, padded to an even length."""
    body = (bytes([0]) + _both32(sector) + _both32(size) + bytes(7) + bytes([FOLDER_FLAG if folder else 0, 0, 0])
            + _both16(1) + bytes([len(name)]) + name)
    body += bytes((len(body) + 1) % 2)  # the length byte counts the pad byte
    return bytes([len(body) + 1]) + body


def _folder(own, parent, entries):
    """The sector of a folder: the records of the folder itself and its parent, then the entries sorted by name."""
    data = _record(b"\x00", *own, True) + _record(b"\x01", *parent, True)
    data += b"".join(_record(name, sector, size, folder) for name, sector, size, folder in sorted(entries))
    assert len(data) <= SECTOR, "a folder of the test image has one sector"
    return data.ljust(SECTOR, b"\0")


def _sectors(size):
    return max(1, -(-size // SECTOR))


def _bcd(value):
    return (value // 10) << 4 | value % 10


def _raw(index, data, mode):
    """A raw sector: the sync pattern, the address (minute, second, frame in BCD), the mode, the data, the tail."""
    frame = index + LEAD_IN
    address = bytes([_bcd(frame // FRAMES // SECONDS), _bcd(frame // FRAMES % SECONDS), _bcd(frame % FRAMES)])
    return RAW_SYNC + address + bytes([mode]) + data + bytes(RAW_TAIL)


def build_iso(files: dict[str, bytes], raw: bool, mode: int = 1) -> bytes:
    """An image with the files ("FTL", "DATA/GRAPHICS.DAT"); raw=True gives sectors of 2352 bytes with mode."""
    folders = sorted({path.split("/")[0] for path in files if "/" in path})
    folder_sector = {name: ROOT_SECTOR + 1 + i for i, name in enumerate(folders)}
    next_sector = ROOT_SECTOR + 1 + len(folders)
    placed = {}
    for path, data in files.items():
        placed[path] = next_sector
        next_sector += _sectors(len(data))
    total = next_sector

    root = (ROOT_SECTOR, SECTOR)
    entries = {name: [] for name in [""] + folders}
    for path, data in files.items():
        folder, _, name = path.rpartition("/")
        entries[folder].append(((name + ";1").encode(), placed[path], len(data), False))
    entries[""] += [(name.encode(), folder_sector[name], SECTOR, True) for name in folders]

    descriptor = bytearray(SECTOR)
    descriptor[0:7] = b"\x01CD001\x01"
    descriptor[80:88] = _both32(total)  # the volume space size
    descriptor[128:132] = _both16(SECTOR)  # the logical block size
    descriptor[156:190] = _record(b"\x00", *root, True)
    terminator = b"\xffCD001\x01".ljust(SECTOR, b"\0")

    sectors = [bytes(SECTOR)] * SYSTEM_AREA + [bytes(descriptor), terminator, _folder(root, root, entries[""])]
    sectors += [_folder((folder_sector[name], SECTOR), root, entries[name]) for name in folders]
    for data in files.values():
        sectors += [data[at:at + SECTOR].ljust(SECTOR, b"\0") for at in range(0, _sectors(len(data)) * SECTOR, SECTOR)]
    assert len(sectors) == total
    if raw:
        return b"".join(_raw(i, data, mode) for i, data in enumerate(sectors))
    return b"".join(sectors)
