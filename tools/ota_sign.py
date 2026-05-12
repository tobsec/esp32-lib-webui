#!/usr/bin/env python3
"""Sign (and optionally zlib-compress) an ESP-IDF firmware image with Ed25519.

Output layout (consumed by reader_ota_recv.cpp + controller HTTP handler):

    [body || signature(64)]                                -- when --compress is OFF
    ["HKZG" + uncompressed_size(LE32) + zlib(body) || signature(64)] -- when --compress is ON

The HKZG magic + size header is added so the controller's HTTP upload
handler can detect compressed mode and announce both wire-payload size
and inflated size to the reader via OtaBegin without needing a separate
query param. Signature is ALWAYS over the uncompressed body — verify
path on reader stays identical (partition contents = uncompressed bin).

Why zlib and not gzip: ESP-IDF's bundled miniz exposes tinfl_decompress
with TINFL_FLAG_PARSE_ZLIB_HEADER natively (zlib's 2-byte header + 4-byte
adler32 trailer). Gzip would require stripping its 18-byte wrapper
manually since miniz has MINIZ_NO_ZLIB_APIS — zlib avoids that complexity.

Compression note (Phase C, #105): a 1.89 MB reader.bin shrinks to
~1.18 MB (37%), cutting OTA wire-time from ~171 s to ~108 s on
115200-baud RS485 with the current 8-chunk burst. Reader needs to know
the inflated size to size the esp_ota_begin partition wipe — sent via
the new `uncompressed_size` field in the OtaBegin protocol message
(0 = uncompressed, non-zero = gzip-compressed).

Usage:
    python tools/ota_sign.py build/foo.bin
    python tools/ota_sign.py build/foo.bin --compress
    python tools/ota_sign.py build/foo.bin --key tools/.ota_signing_key -o foo.signed.bin

Generate a fresh key once (do this on the build host, never check in):
    python -c "import os, pathlib; p=pathlib.Path('tools/.ota_signing_key'); p.write_bytes(os.urandom(32)); print('wrote', p)"
"""
import argparse
import os
import sys
import zlib
from pathlib import Path

try:
    from nacl.signing import SigningKey
except ImportError:
    sys.stderr.write(
        "ota_sign.py requires PyNaCl. Install with:\n"
        "    pip install pynacl\n"
    )
    sys.exit(2)


def main() -> int:
    ap = argparse.ArgumentParser(description="Sign (+ optionally gzip) an esp-idf .bin")
    ap.add_argument("image", help="path to the unsigned .bin")
    ap.add_argument("--key",  default="tools/.ota_signing_key",
                    help="32-byte raw Ed25519 seed file (default: tools/.ota_signing_key)")
    ap.add_argument("-o", "--out", default=None,
                    help="output path (default: <image>.signed.bin alongside the input)")
    ap.add_argument("--compress", action="store_true",
                    help="zlib-compress the body before signing — sig is still over the "
                         "uncompressed bytes. Output ends in .signed.bin.zz by default.")
    args = ap.parse_args()

    image_path = Path(args.image)
    key_path   = Path(args.key)
    if args.out:
        out_path = Path(args.out)
    elif args.compress:
        out_path = (image_path.with_suffix(".signed.bin.zz")
                    if image_path.suffix == ".bin"
                    else image_path.with_name(image_path.name + ".signed.zz"))
    else:
        out_path = (image_path.with_suffix(".signed.bin")
                    if image_path.suffix == ".bin"
                    else image_path.with_name(image_path.name + ".signed"))

    if not image_path.exists():
        ap.error(f"image not found: {image_path}")
    if not key_path.exists():
        sys.stderr.write(
            f"key file not found: {key_path}\n"
            "  generate with:\n"
            f"    python -c \"import os; open('{key_path}', 'wb').write(os.urandom(32))\"\n"
        )
        return 3

    seed = key_path.read_bytes()
    if len(seed) != 32:
        sys.stderr.write(f"key file must be 32 bytes raw seed, got {len(seed)}\n")
        return 3

    sk   = SigningKey(seed)
    body = image_path.read_bytes()
    sig  = sk.sign(body).signature  # signature is ALWAYS over uncompressed bytes
    assert len(sig) == 64

    if args.compress:
        # Level 9 max; OTA happens once-in-a-while so spend the CPU
        # at build time, not on the reader. zlib stream = 2-byte header
        # + raw deflate + 4-byte adler32 trailer.
        zz = zlib.compress(body, level=9)
        # Header: 4-byte magic "HKZG" + 4-byte uncompressed size (LE)
        hdr = b"HKZG" + len(body).to_bytes(4, "little")
        wire_payload = hdr + zz
    else:
        wire_payload = body

    with out_path.open("wb") as f:
        f.write(wire_payload)
        f.write(sig)

    pk_hex = sk.verify_key.encode().hex().upper()
    if args.compress:
        ratio = len(wire_payload) / len(body) * 100
        print(f"signed+compressed: {out_path}")
        print(f"  uncompressed body: {len(body)} B")
        print(f"  wire payload:      {len(wire_payload)} B ({ratio:.1f}% of original, incl 8 B HKZG header + 6 B zlib wrap)")
        print(f"  total on wire:     {len(wire_payload) + 64} B (incl. 64 B sig)")
    else:
        print(f"signed: {out_path}  ({len(body)} body + 64 sig = {len(body) + 64} bytes)")
    print(f"public key (paste into secrets.h HK_OTA_SIGNING_PUBKEY):")
    print(f"  {pk_hex}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
