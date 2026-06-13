#!/usr/bin/env python3
import pathlib
import struct
import sys


SECTOR_SIZE = 512
MAGIC = b"OSAIINITFS1"
MAX_FILES = 4
PATH_MAX = 56
HEADER_SECTOR = 1
DATA_OFFSET = 4096


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def write_entry(path: str, offset: int, size: int, flags: int) -> bytes:
    encoded = path.encode("ascii")
    if len(encoded) >= PATH_MAX:
        raise SystemExit(f"path too long for initfs: {path}")
    return (
        encoded.ljust(PATH_MAX, b"\0")
        + struct.pack("<QQII", offset, size, flags, 0)
    )


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: create-initfs.py <image> <init-elf> <config>", file=sys.stderr)
        return 2

    image = pathlib.Path(sys.argv[1])
    init_elf = pathlib.Path(sys.argv[2]).read_bytes()
    config = pathlib.Path(sys.argv[3]).read_bytes()
    files = [
        ("/init", init_elf, 1),
        ("/etc/osai-init.conf", config, 0),
    ]
    if len(files) > MAX_FILES:
        raise SystemExit("too many initfs files")

    offset = DATA_OFFSET
    entries = []
    payloads = []
    for path, content, flags in files:
        offset = align(offset, SECTOR_SIZE)
        entries.append(write_entry(path, offset, len(content), flags))
        payloads.append((offset, content))
        offset += len(content)

    header = MAGIC.ljust(16, b"\0") + struct.pack("<II", 1, len(files))
    header += b"".join(entries)
    header = header.ljust(SECTOR_SIZE, b"\0")
    with image.open("r+b") as f:
        f.seek(HEADER_SECTOR * SECTOR_SIZE)
        f.write(header)
        for payload_offset, content in payloads:
            f.seek(payload_offset)
            f.write(content)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
