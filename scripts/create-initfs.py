#!/usr/bin/env python3
import pathlib
import struct
import sys


SECTOR_SIZE = 512
MAGIC = b"OSAIROFS2"
VERSION = 2
MAX_FILES = 4
PATH_MAX = 64
HEADER_SECTOR = 1
DATA_OFFSET = 4096
FLAG_READ_ONLY = 1
ENTRY_FLAG_EXECUTABLE = 1
ENTRY_FLAG_MANIFEST = 2
ENTRY_TYPE_FILE = 1
FNV1A64_OFFSET = 0xCBF29CE484222325
FNV1A64_PRIME = 0x100000001B3


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def fnv1a64(content: bytes) -> int:
    value = FNV1A64_OFFSET
    for byte in content:
        value ^= byte
        value = (value * FNV1A64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def write_entry(path: str, offset: int, size: int, flags: int, content_hash: int) -> bytes:
    encoded = path.encode("ascii")
    if len(encoded) >= PATH_MAX:
        raise SystemExit(f"path too long for initfs: {path}")
    return (
        encoded.ljust(PATH_MAX, b"\0")
        + struct.pack("<QQIIQ", offset, size, flags, ENTRY_TYPE_FILE, content_hash)
    )


def main() -> int:
    if len(sys.argv) != 6:
        print(
            "usage: create-initfs.py <image> <init-elf> <service-manager-elf> <config> <service-descriptor>",
            file=sys.stderr,
        )
        return 2

    image = pathlib.Path(sys.argv[1])
    init_elf = pathlib.Path(sys.argv[2]).read_bytes()
    service_manager_elf = pathlib.Path(sys.argv[3]).read_bytes()
    config = pathlib.Path(sys.argv[4]).read_bytes()
    service_descriptor = pathlib.Path(sys.argv[5]).read_bytes()
    files = [
        ("/init", init_elf, ENTRY_FLAG_EXECUTABLE),
        ("/bin/service-manager", service_manager_elf, ENTRY_FLAG_EXECUTABLE),
        ("/etc/osai-init.conf", config, ENTRY_FLAG_MANIFEST),
        ("/etc/services/source-index.svc", service_descriptor, 0),
    ]
    if len(files) > MAX_FILES:
        raise SystemExit("too many initfs files")

    offset = DATA_OFFSET
    entries = []
    payloads = []
    for path, content, flags in files:
        offset = align(offset, SECTOR_SIZE)
        entries.append(write_entry(path, offset, len(content), flags, fnv1a64(content)))
        payloads.append((offset, content))
        offset += len(content)

    image_size = align(offset, SECTOR_SIZE)
    manifest_index = 2
    header = MAGIC.ljust(16, b"\0") + struct.pack(
        "<IIIIIIQQ",
        VERSION,
        SECTOR_SIZE,
        SECTOR_SIZE,
        len(files),
        manifest_index,
        FLAG_READ_ONLY,
        DATA_OFFSET,
        image_size,
    )
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
