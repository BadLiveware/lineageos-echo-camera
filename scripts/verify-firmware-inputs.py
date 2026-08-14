#!/usr/bin/env python3
"""Verify proprietary extraction inputs against a preserved firmware manifest."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
from typing import List


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("android_root", type=Path)
    parser.add_argument("device", choices=("checkers", "crown"))
    parser.add_argument("--include-common", action="store_true")
    parser.add_argument("--section")
    parser.add_argument("--report", type=Path)
    return parser.parse_args()


def read_trusted_manifest(path: Path) -> dict[str, str]:
    trusted: dict[str, str] = {}
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        if not line:
            continue
        try:
            digest, relative_path = line.split(maxsplit=1)
        except ValueError as error:
            raise ValueError(f"{path}:{line_number}: malformed checksum row") from error
        if relative_path in trusted:
            raise ValueError(f"{path}:{line_number}: duplicate path {relative_path}")
        trusted[relative_path] = digest
    return trusted


def iter_entries(path: Path, section: str | None):
    active = section is None
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        entry = line.strip()
        if entry.startswith("#"):
            heading = entry[1:].strip() if entry.startswith("#") else entry
            active = section is None or heading == section
            continue
        if not active or not entry:
            continue
        unhashed = entry.split("|", 1)[0]
        source, separator, destination = unhashed.partition(":")
        source = source.lstrip("/")
        destination = (destination if separator else source).lstrip("/")
        yield line_number, source, destination


def source_relative_path(source: str) -> str:
    return source if source.startswith("system/") else f"system/{source}"


def main() -> int:
    args = parse_arguments()
    firmware_root = args.firmware_root.resolve()
    android_root = args.android_root.resolve()
    trusted_path = firmware_root / "SHA256SUMS"
    if not trusted_path.is_file():
        raise SystemExit(f"missing trusted firmware manifest: {trusted_path}")
    trusted = read_trusted_manifest(trusted_path)

    lists = [(args.device, android_root / f"device/amazon/{args.device}/proprietary-files.txt")]
    if args.include_common:
        lists.insert(
            0,
            (
                "mt8163-common",
                android_root / "device/amazon/mt8163-common/proprietary-files.txt",
            ),
        )
    report_rows: List[str] = []
    verified = 0
    for owner, list_path in lists:
        if not list_path.is_file():
            raise SystemExit(f"missing proprietary list: {list_path}")
        for line_number, source, destination in iter_entries(list_path, args.section):
            relative = source_relative_path(source)
            source_path = firmware_root / relative
            if not source_path.is_file():
                raise SystemExit(
                    f"{list_path}:{line_number}: missing firmware input {relative}"
                )
            expected = trusted.get(relative)
            if expected is None:
                raise SystemExit(
                    f"{list_path}:{line_number}: untrusted firmware input {relative}"
                )
            actual = hashlib.sha256(source_path.read_bytes()).hexdigest()
            if actual != expected:
                raise SystemExit(
                    f"{list_path}:{line_number}: checksum mismatch for {relative}: "
                    f"expected {expected}, got {actual}"
                )
            report_rows.append(
                f"{actual}  {relative}\t"
                f"vendor/amazon/{owner}/proprietary/{destination}"
            )
            verified += 1

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text("\n".join(sorted(report_rows)) + "\n")
    print(f"Verified {verified} {args.device} firmware inputs against {trusted_path}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
