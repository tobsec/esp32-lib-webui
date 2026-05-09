#!/usr/bin/env python3
"""Sign an ESP-IDF firmware image with an Ed25519 key.

Output layout (consumed by reader_core/src/web/ota.cpp):

    body || signature(64 bytes)

`body` is the regular esp-idf .bin (factory-flashable as is).
`signature` is computed over the entire body with NaCl Ed25519
(detached) using a 32-byte raw seed loaded from a key file.

Usage:
    python tools/ota_sign.py path/to/build/foo.bin
    python tools/ota_sign.py path/to/build/foo.bin --key tools/.ota_signing_key
    python tools/ota_sign.py path/to/build/foo.bin -o build/foo.signed.bin

Generate a fresh key once (do this on the build host, never check in):
    python -c "import os, pathlib; p=pathlib.Path('tools/.ota_signing_key'); p.write_bytes(os.urandom(32)); print('wrote', p)"

The script then prints the matching public key as a 64-character hex
string — paste that into reader_standalone/main/secrets.h as
HK_OTA_SIGNING_PUBKEY.
"""
import argparse
import os
import sys
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
    ap = argparse.ArgumentParser(description="Sign an esp-idf .bin with Ed25519")
    ap.add_argument("image", help="path to the unsigned .bin")
    ap.add_argument("--key",  default="tools/.ota_signing_key",
                    help="32-byte raw Ed25519 seed file (default: tools/.ota_signing_key)")
    ap.add_argument("-o", "--out", default=None,
                    help="output path (default: <image>.signed.bin alongside the input)")
    args = ap.parse_args()

    image_path = Path(args.image)
    key_path   = Path(args.key)
    out_path   = Path(args.out) if args.out else (
        image_path.with_suffix(".signed.bin") if image_path.suffix == ".bin"
        else image_path.with_name(image_path.name + ".signed")
    )

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
    sig  = sk.sign(body).signature  # detached 64-byte signature
    assert len(sig) == 64

    with out_path.open("wb") as f:
        f.write(body)
        f.write(sig)

    pk_hex = sk.verify_key.encode().hex().upper()
    print(f"signed: {out_path}  ({len(body)} body + 64 sig = {len(body) + 64} bytes)")
    print(f"public key (paste into secrets.h HK_OTA_SIGNING_PUBKEY):")
    print(f"  {pk_hex}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
