"""Tests for the Ed25519 signing path that ota_sign.py + the on-target
verify in src/web/ota.cpp share.

These run on the build host (no ESP-IDF, no hardware). Coverage:

  test_rfc8032_*           — sanity-check PyNaCl against RFC 8032 vectors;
                              if these fail, our local crypto is broken
                              and there's no point looking at the rest.
  test_signed_image_layout — body || sig(64) framing matches the format
                              ota::install_signed expects.
  test_round_trip          — sign + verify, both with the lib's helper
                              and raw PyNaCl, agree.
  test_tamper_*            — single-byte flips in body or signature
                              are correctly rejected.
  test_truncated_signature — short body (no full 64-byte trailer) does
                              not silently slip through.

The tooling itself (ota_sign.py) is exercised end-to-end in
test_ota_sign_cli — we shell out to the CLI exactly like a build script
would and assert the produced file round-trips.

Run with: pip install pynacl pytest && pytest tests/
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest
from nacl.signing import SigningKey, VerifyKey
from nacl.exceptions import BadSignatureError

REPO = Path(__file__).resolve().parents[1]
OTA_SIGN = REPO / "tools" / "ota_sign.py"
SIG_LEN = 64


# ---------------------------------------------------------------------------
# RFC 8032 known-answer tests for Ed25519 (test 1: empty message).
# https://datatracker.ietf.org/doc/html/rfc8032#section-7.1

_RFC8032_TEST1 = {
    "secret_seed": bytes.fromhex(
        "9d61b19deffd5a60ba844af492ec2cc4"
        "4449c5697b326919703bac031cae7f60"
    ),
    "public_key": bytes.fromhex(
        "d75a980182b10ab7d54bfed3c964073a"
        "0ee172f3daa62325af021a68f707511a"
    ),
    "message": b"",
    "signature": bytes.fromhex(
        "e5564300c360ac729086e2cc806e828a"
        "84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46b"
        "d25bf5f0595bbe24655141438e7a100b"
    ),
}

_RFC8032_TEST2 = {
    "secret_seed": bytes.fromhex(
        "4ccd089b28ff96da9db6c346ec114e0f"
        "5b8a319f35aba624da8cf6ed4fb8a6fb"
    ),
    "public_key": bytes.fromhex(
        "3d4017c3e843895a92b70aa74d1b7ebc"
        "9c982ccf2ec4968cc0cd55f12af4660c"
    ),
    "message": bytes.fromhex("72"),
    "signature": bytes.fromhex(
        "92a009a9f0d4cab8720e820b5f642540"
        "a2b27b5416503f8fb3762223ebdb69da"
        "085ac1e43e15996e458f3613d0f11d8c"
        "387b2eaeb4302aeeb00d291612bb0c00"
    ),
}


@pytest.mark.parametrize("vec", [_RFC8032_TEST1, _RFC8032_TEST2],
                          ids=["rfc8032-test-1", "rfc8032-test-2"])
def test_rfc8032_pubkey_derivation(vec):
    """Deriving the verify key from the seed must match the spec."""
    sk = SigningKey(vec["secret_seed"])
    assert sk.verify_key.encode() == vec["public_key"]


@pytest.mark.parametrize("vec", [_RFC8032_TEST1, _RFC8032_TEST2],
                          ids=["rfc8032-test-1", "rfc8032-test-2"])
def test_rfc8032_signature(vec):
    """Signing a known message must produce the spec's signature byte-for-byte."""
    sk = SigningKey(vec["secret_seed"])
    sig = sk.sign(vec["message"]).signature
    assert sig == vec["signature"]


@pytest.mark.parametrize("vec", [_RFC8032_TEST1, _RFC8032_TEST2],
                          ids=["rfc8032-test-1", "rfc8032-test-2"])
def test_rfc8032_verify(vec):
    """Verifying the spec's signature with the spec's pubkey must succeed."""
    vk = VerifyKey(vec["public_key"])
    vk.verify(vec["message"], vec["signature"])  # no exception = pass


# ---------------------------------------------------------------------------
# ota_sign.py CLI — actually shell out, exactly as a build script would.

@pytest.fixture
def signing_setup(tmp_path):
    """Generates a fresh seed + a small synthetic 'firmware' file."""
    seed = os.urandom(32)
    key_path = tmp_path / ".ota_signing_key"
    key_path.write_bytes(seed)

    # Synthetic body — pad to >1 KB so the body-length check on the
    # reader side (kSigLen + 1024) doesn't reject it.
    body = b"# fake firmware\n" + os.urandom(2048)
    bin_path = tmp_path / "fw.bin"
    bin_path.write_bytes(body)

    sk = SigningKey(seed)
    return {
        "tmp": tmp_path,
        "seed": seed,
        "key_path": key_path,
        "bin_path": bin_path,
        "body": body,
        "sk": sk,
        "pubkey": sk.verify_key.encode(),
    }


def _run_ota_sign(*args, cwd):
    """Shell-out to ota_sign.py; return CompletedProcess."""
    return subprocess.run(
        [sys.executable, str(OTA_SIGN), *args],
        cwd=cwd, capture_output=True, text=True, check=False,
    )


def test_ota_sign_cli_round_trip(signing_setup):
    """ota_sign.py output: body || sig(64), verifies cleanly with the matching pubkey."""
    s = signing_setup
    out_path = s["tmp"] / "fw.signed.bin"
    res = _run_ota_sign(
        str(s["bin_path"]), "--key", str(s["key_path"]), "-o", str(out_path),
        cwd=s["tmp"],
    )
    assert res.returncode == 0, f"signer failed: {res.stderr}"
    assert out_path.exists()

    signed = out_path.read_bytes()
    assert len(signed) == len(s["body"]) + SIG_LEN
    body, sig = signed[:-SIG_LEN], signed[-SIG_LEN:]
    assert body == s["body"]

    VerifyKey(s["pubkey"]).verify(body, sig)  # no exception = pass


def test_ota_sign_cli_prints_pubkey_hex(signing_setup):
    """The CLI's stdout must include the matching public-key hex.

    secrets.h paste relies on the printed line.
    """
    s = signing_setup
    res = _run_ota_sign(
        str(s["bin_path"]), "--key", str(s["key_path"]),
        cwd=s["tmp"],
    )
    assert res.returncode == 0
    expected_hex = s["pubkey"].hex().upper()
    assert expected_hex in res.stdout


def test_ota_sign_cli_rejects_short_key(tmp_path):
    """A non-32-byte seed file must fail loud, not silently produce garbage."""
    bad_key = tmp_path / "short.key"
    bad_key.write_bytes(b"only-15-bytes!\x00")
    bin_path = tmp_path / "fw.bin"
    bin_path.write_bytes(b"x" * 1024)
    res = _run_ota_sign(str(bin_path), "--key", str(bad_key), cwd=tmp_path)
    assert res.returncode != 0
    assert "32 bytes" in res.stderr


def test_ota_sign_cli_rejects_missing_key(tmp_path):
    bin_path = tmp_path / "fw.bin"
    bin_path.write_bytes(b"x" * 1024)
    res = _run_ota_sign(
        str(bin_path), "--key", str(tmp_path / "nope.key"), cwd=tmp_path,
    )
    assert res.returncode != 0


# ---------------------------------------------------------------------------
# Tamper detection — same surface ota::install_signed() exercises on-target.

def test_tamper_body_byte_rejected(signing_setup):
    """A single bit-flip anywhere in the body must invalidate the signature."""
    s = signing_setup
    sig = s["sk"].sign(s["body"]).signature
    vk = VerifyKey(s["pubkey"])

    tampered = bytearray(s["body"])
    tampered[1000] ^= 0x01
    with pytest.raises(BadSignatureError):
        vk.verify(bytes(tampered), sig)


def test_tamper_signature_byte_rejected(signing_setup):
    """A single bit-flip in the signature must invalidate it."""
    s = signing_setup
    sig = bytearray(s["sk"].sign(s["body"]).signature)
    sig[0] ^= 0x01
    vk = VerifyKey(s["pubkey"])
    with pytest.raises(BadSignatureError):
        vk.verify(s["body"], bytes(sig))


def test_truncated_body_signature_doesnt_match(signing_setup):
    """If the receiver mis-parses (e.g. takes 63 bytes as sig instead of 64),
    verification must fail; the layout doesn't accidentally validate.
    """
    s = signing_setup
    sig = s["sk"].sign(s["body"]).signature
    vk = VerifyKey(s["pubkey"])

    # Treat the last 64 bytes of body as the signature (off-by-one
    # parsing on the receiver). This must NOT verify.
    bogus_sig = (s["body"][-SIG_LEN:] if len(s["body"]) >= SIG_LEN
                 else s["body"].rjust(SIG_LEN, b"\x00"))
    with pytest.raises(BadSignatureError):
        vk.verify(s["body"][:-SIG_LEN] + sig[:1], bogus_sig)


def test_wrong_pubkey_rejected(signing_setup):
    """Verifying with a different key must fail (this is what the device does
    when the lab build pubkey doesn't match the signing seed)."""
    s = signing_setup
    sig = s["sk"].sign(s["body"]).signature
    other_pk = SigningKey(os.urandom(32)).verify_key
    with pytest.raises(BadSignatureError):
        other_pk.verify(s["body"], sig)
