"""Build and serve a deterministic MagicUSB lab release. No external packages."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import struct
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

BLOCK_SIZE = 512
BLOCK_COUNT = 128


def put16(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buffer, offset, value)


def put32(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buffer, offset, value)


def build_image(path: Path, release: str) -> bytes:
    disk = bytearray(BLOCK_SIZE * BLOCK_COUNT)
    disk[0:3] = b"\xeb\x3c\x90"
    disk[3:11] = b"MSDOS5.0"
    put16(disk, 11, BLOCK_SIZE)
    disk[13] = 1
    put16(disk, 14, 1)
    disk[16] = 1
    put16(disk, 17, 16)
    put16(disk, 19, BLOCK_COUNT)
    disk[21] = 0xF8
    put16(disk, 22, 1)
    put16(disk, 24, 1)
    put16(disk, 26, 1)
    disk[36] = 0x80
    disk[38] = 0x29
    put32(disk, 39, 0x4D555342)
    disk[43:54] = b"MAGICUSB   "
    disk[54:62] = b"FAT12   "
    disk[510:512] = b"\x55\xaa"
    disk[512:517] = b"\xf8\xff\xff\xff\x0f"

    message = (
        "MagicUSB lab update\r\n"
        f"Release: {release}\r\n"
        "Downloaded into the inactive slot and verified before activation.\r\n"
    ).encode("ascii")
    root = 2 * BLOCK_SIZE
    disk[root : root + 11] = b"UPDATE  TXT"
    disk[root + 11] = 0x21
    put16(disk, root + 26, 2)
    put32(disk, root + 28, len(message))
    disk[3 * BLOCK_SIZE : 3 * BLOCK_SIZE + len(message)] = message
    path.write_bytes(disk)
    return bytes(disk)


def canonical_manifest(manifest: dict[str, object]) -> bytes:
    scope = manifest.get("scope", {})
    return (
        f"schema_version={manifest['schema_version']}\n"
        f"release={manifest['release']}\n"
        f"minimum_firmware={manifest['minimum_firmware']}\n"
        f"size={manifest['size']}\n"
        f"sha256={manifest['sha256']}\n"
        f"download_url={manifest['download_url']}\n"
        f"site={scope.get('site', '')}\n"
        f"device_id={scope.get('device_id', '')}\n"
    ).encode("ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--advertise")
    parser.add_argument("--base-url", help="HTTPS or HTTP directory URL used in the manifest")
    parser.add_argument("--port", type=int, default=8088)
    parser.add_argument("--release", default="2026.08.20.1")
    parser.add_argument("--minimum-firmware", default="0.1.0")
    parser.add_argument("--site", default="LAB")
    parser.add_argument("--device-id", default="")
    parser.add_argument("--signing-key", type=Path, help="raw 32-byte P-256 private scalar")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if not args.base_url and not args.advertise:
        parser.error("one of --base-url or --advertise is required")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.minimum_firmware):
        parser.error("--minimum-firmware must be major.minor.patch")
    if args.site and not re.fullmatch(r"[A-Z0-9_-]{1,32}", args.site):
        parser.error("--site must contain 1-32 uppercase letters, digits, '_' or '-'")
    if len(args.device_id) > 64 or any(ord(char) < 0x21 or ord(char) > 0x7e for char in args.device_id):
        parser.error("--device-id must contain at most 64 printable non-space ASCII characters")

    args.output.mkdir(parents=True, exist_ok=True)
    image = build_image(args.output / "release.fat", args.release)
    manifest = {
        "schema_version": 1,
        "release": args.release,
        "minimum_firmware": args.minimum_firmware,
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "download_url": (f"{args.base_url.rstrip('/')}/release.fat" if args.base_url else
                         f"http://{args.advertise}:{args.port}/release.fat"),
        "scope": {"site": args.site},
    }
    if args.device_id:
        manifest["scope"]["device_id"] = args.device_id
    if args.signing_key:
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

        private_seed = args.signing_key.read_bytes()
        if len(private_seed) != 32:
            parser.error("--signing-key must contain exactly 32 raw bytes")
        private_key = ec.derive_private_key(int.from_bytes(private_seed, "big"), ec.SECP256R1())
        der_signature = private_key.sign(canonical_manifest(manifest), ec.ECDSA(hashes.SHA256()))
        r, s = decode_dss_signature(der_signature)
        signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")
        manifest["signature"] = base64.b64encode(signature).decode("ascii")
    (args.output / "manifest.json").write_text(json.dumps(manifest, separators=(",", ":")), encoding="ascii")
    print(json.dumps({"release": args.release, "size": len(image), "sha256": manifest["sha256"]}))
    if not args.build_only:
        handler = lambda *values, **kwargs: SimpleHTTPRequestHandler(*values, directory=str(args.output), **kwargs)
        ThreadingHTTPServer((args.host, args.port), handler).serve_forever()


if __name__ == "__main__":
    main()
