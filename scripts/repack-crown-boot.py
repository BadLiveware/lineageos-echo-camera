#!/usr/bin/env python3
"""Replace a Crown boot image kernel while preserving the legacy v0 format."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

BOOT_MAGIC = b"ANDROID!"
PAGE_SIZE_OFFSET = 36
KERNEL_SIZE_OFFSET = 8
RAMDISK_SIZE_OFFSET = 16
SECOND_SIZE_OFFSET = 24
HEADER_VERSION_OFFSET = 40
IMAGE_ID_OFFSET = 576
IMAGE_ID_SIZE = 32
LEGACY_HEADER_SIZE = 1632
LEGACY_DT_SIZE = struct.pack("<I", 0)


def align(value: int, page_size: int) -> int:
    return (value + page_size - 1) // page_size * page_size


def sized_hash(sha: "hashlib._Hash", payload: bytes) -> None:
    sha.update(payload)
    sha.update(struct.pack("<I", len(payload)))


def repack(template: bytes, kernel: bytes) -> bytes:
    if template[:8] != BOOT_MAGIC:
        raise ValueError("template is not an Android boot image")

    kernel_size = struct.unpack_from("<I", template, KERNEL_SIZE_OFFSET)[0]
    ramdisk_size = struct.unpack_from("<I", template, RAMDISK_SIZE_OFFSET)[0]
    second_size = struct.unpack_from("<I", template, SECOND_SIZE_OFFSET)[0]
    page_size = struct.unpack_from("<I", template, PAGE_SIZE_OFFSET)[0]
    header_version = struct.unpack_from("<I", template, HEADER_VERSION_OFFSET)[0]
    if header_version != 0:
        raise ValueError(f"expected boot image header v0, got v{header_version}")
    if page_size == 0 or page_size & (page_size - 1):
        raise ValueError(f"invalid page size {page_size}")
    if page_size < LEGACY_HEADER_SIZE:
        raise ValueError(f"page size {page_size} is too small for a v0 header")
    if len(template) % page_size:
        raise ValueError("template length is not page-aligned")

    kernel_offset = page_size
    ramdisk_offset = kernel_offset + align(kernel_size, page_size)
    second_offset = ramdisk_offset + align(ramdisk_size, page_size)
    end_offset = second_offset + align(second_size, page_size)
    if len(template) < end_offset:
        raise ValueError("template is truncated")
    ramdisk = template[ramdisk_offset : ramdisk_offset + ramdisk_size]
    second = template[second_offset : second_offset + second_size]

    header = bytearray(template[:page_size])
    struct.pack_into("<I", header, KERNEL_SIZE_OFFSET, len(kernel))
    sha = hashlib.sha1()
    sized_hash(sha, kernel)
    sized_hash(sha, ramdisk)
    sized_hash(sha, second)
    # Crown's legacy mkbootimg includes the zero dt_size field after second.
    sha.update(LEGACY_DT_SIZE)
    header[IMAGE_ID_OFFSET : IMAGE_ID_OFFSET + IMAGE_ID_SIZE] = sha.digest().ljust(
        IMAGE_ID_SIZE, b"\0"
    )

    output = bytearray(header)
    output.extend(kernel)
    output.extend(b"\0" * (align(len(output), page_size) - len(output)))
    output.extend(ramdisk)
    output.extend(b"\0" * (align(len(output), page_size) - len(output)))
    output.extend(second)
    output.extend(b"\0" * (align(len(output), page_size) - len(output)))

    # A v0 template has no payload after the aligned second stage. Refuse to
    # silently discard signatures or device-specific trailers if one appears.
    if any(template[end_offset:]):
        raise ValueError("template contains a non-zero trailer; refusing to discard it")
    if len(output) > len(template):
        raise ValueError(
            f"repacked image is {len(output)} bytes, exceeding the "
            f"{len(template)}-byte template budget"
        )
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.write_bytes(repack(args.template.read_bytes(), args.kernel.read_bytes()))


if __name__ == "__main__":
    main()
