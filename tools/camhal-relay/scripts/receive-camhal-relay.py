#!/usr/bin/env python3
"""Receive bounded NV21 frames from camhal_host's ADB-forwarded relay."""

from __future__ import annotations

import argparse
import socket
import struct
import time
from pathlib import Path

HEADER = struct.Struct("<IHHQQIIIII")
MAGIC = 0x594C5243  # b"CRLY" as a little-endian uint32
VERSION = 1
PIXEL_FORMAT_NV21 = 0x3132564E  # b"NV21" as a little-endian uint32
MAX_PAYLOAD = 32 * 1024 * 1024


def receive_exact(connection: socket.socket, byte_count: int) -> bytes:
    chunks: list[bytes] = []
    remaining = byte_count
    while remaining:
        chunk = connection.recv(remaining)
        if not chunk:
            raise EOFError(f"relay closed with {remaining} bytes still expected")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def connect_with_retry(host: str, port: int, timeout: float) -> socket.socket:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        try:
            connection = socket.create_connection((host, port), timeout=2.0)
            connection.settimeout(5.0)
            return connection
        except OSError as error:
            last_error = error
            time.sleep(0.1)
    raise TimeoutError(f"could not connect to {host}:{port}: {last_error}")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive framed 640x480 NV21 data from camhal_host"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=57321)
    parser.add_argument("--frames", type=int, default=1)
    parser.add_argument("--output-dir", type=Path, default=Path("results/relay"))
    parser.add_argument("--connect-timeout", type=float, default=10.0)
    arguments = parser.parse_args()
    if not 1 <= arguments.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if not 1 <= arguments.frames <= 10000:
        parser.error("--frames must be between 1 and 10000")
    if not 0 < arguments.connect_timeout <= 300:
        parser.error("--connect-timeout must be between 0 and 300 seconds")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    arguments.output_dir.mkdir(parents=True, exist_ok=True)

    with connect_with_retry(
        arguments.host, arguments.port, arguments.connect_timeout
    ) as connection:
        for _ in range(arguments.frames):
            raw_header = receive_exact(connection, HEADER.size)
            (
                magic,
                version,
                header_bytes,
                sequence,
                timestamp_ns,
                width,
                height,
                pixel_format,
                payload_bytes,
                dropped_frames,
            ) = HEADER.unpack(raw_header)
            if magic != MAGIC or version != VERSION or header_bytes != HEADER.size:
                raise ValueError(
                    f"invalid relay header magic=0x{magic:08x} "
                    f"version={version} bytes={header_bytes}"
                )
            if pixel_format != PIXEL_FORMAT_NV21:
                raise ValueError(f"unsupported pixel format 0x{pixel_format:08x}")
            expected_bytes = width * height * 3 // 2
            if payload_bytes != expected_bytes or payload_bytes > MAX_PAYLOAD:
                raise ValueError(
                    f"invalid payload size {payload_bytes}; expected {expected_bytes}"
                )

            payload = receive_exact(connection, payload_bytes)
            output = arguments.output_dir / f"frame-{sequence:06d}.nv21"
            output.write_bytes(payload)
            print(
                f"sequence={sequence} timestamp_ns={timestamp_ns} "
                f"size={width}x{height} bytes={payload_bytes} "
                f"dropped={dropped_frames} output={output}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
