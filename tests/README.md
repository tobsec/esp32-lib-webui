# Tests

Host-side pytest suite for the security-critical parts (ed25519 signing,
image format, tamper detection).

```bash
pip install pynacl pytest
pytest tests/
```

What's covered:

- **RFC 8032 known-answer vectors** for Ed25519 — sanity that PyNaCl
  agrees with the spec; if these fail, the host crypto is broken and
  there's no point looking at the rest.
- **`ota_sign.py` CLI round-trip** — actually shells out to the tool
  exactly like a build script would; checks the output layout
  (`body || sig(64)`), the printed pubkey hex (used for the secrets.h
  paste), and the failure paths (short key, missing key).
- **Tamper detection** — single-byte flips in body/signature, off-by-one
  signature parsing, wrong pubkey — all expected to fail verification,
  asserting that the verify path on the device (libsodium
  `crypto_sign_verify_detached` in `src/web/ota.cpp`) won't accept any
  of these either.

What's NOT covered:

- The on-target verify path itself — that's the C++ `webui::ota::install_signed`
  flow, which needs ESP-IDF + a target. Build the `examples/minimal/`
  project and POST a tampered signed bin to `/api/ota/upload` for the
  device-side smoke; the test here only validates the host signer.
- The streaming chunked recv + `esp_ota_*` write path — that's
  integration territory.

Why we test the signer rather than the verifier:

Both sides use the same Ed25519 algorithm (PyNaCl on the host,
libsodium on the device — both call into `ref10` at the bottom). If
the signer produces a signature that PyNaCl-the-verifier accepts,
libsodium will accept it too. The device-side bugs we worry about are
in the framing logic (`body || sig(64)` parsing, NUL-strip), not in
the crypto math.
