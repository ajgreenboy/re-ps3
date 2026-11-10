# Local PUP extraction

`re-ps3-native --firmware-extract "C:/path/PS3UPDAT.PUP@C:/path/dev_flash"`

The destination is the **contents** of dev_flash, e.g. `vsh/resource`, not a
directory containing another `dev_flash` directory. No download is performed.
Success prints verified entry/package/byte counts and destination; failure exits
2. A verified PUP with no dev_flash packages fails rather than claiming installation.

## Keys (never commit populated values)

Copy `src/firmware_keys.example.inc` to the gitignored `src/firmware_keys.inc`,
replace the zero arrays with the public RPCS3 `Crypto/key_vault.h` declarations,
and rebuild. Only `PUP_KEY` (64 bytes), `SCEPKG_ERK` (32 bytes), and `SCEPKG_RIV`
(16 bytes) are used. There are no PKG AES keys. Builds without the local include
compile with zero placeholders and report the missing-key error at extraction.

Alternatively set `XMB_FIRMWARE_KEYS` to a local `.inc` file at runtime. It accepts
the same three C array declarations, with exactly the required number of decimal
or `0x` byte literals; comments and trailing commas are allowed. This overrides
compiled keys. It is parsed as data, not compiled/executed. Keep alternative key
files outside the repository. Local builds with the private include embed its
keys in the binary: do not publish those binaries under the no-keys rule.

## Format and safety

Ground truth: RPCS3 `Loader/PUP.h:9-32`, `PUP.cpp:29-59,82-111`;
`Crypto/unself.h:161-200,370-382`; `Crypto/unself.cpp:626-817`;
`rpcs3qt/main_window.cpp:1682-1697`.

The 48-byte PUP header contains the 8 literal magic bytes followed by five
big-endian u64 values. File and hash tables each have `file_count` 32-byte
records. Offsets/lengths and duplicate IDs are checked and every file's HMAC-SHA1
is verified (hashes matched by entry ID) before the update TAR is processed.
The update is entry `0x300`; package names are discovered by `dev_flash_` prefix.

For retail SCE packages, metadata info uses AES-256-CBC with SCEPKG_ERK/RIV.
Its embedded AES-128 key/IV decrypt metadata headers in CTR mode. Section key/IV
indices address the decrypted key table. Section encryption flag 3 selects
AES-128-CTR; compression flag 2 selects zlib/gzip inflate. Section index 2 is the
dev_flash TAR. Debug metadata info is already plaintext, as in RPCS3.
Decompression uses Qt's already-bundled zlib, not a new downloaded dependency.

TAR requires ustar headers, checksums, bounded sizes and a two-block terminator.
Only regular files/directories are accepted; links and devices fail. Paths with
`..`, absolute prefixes, backslashes, drive letters, ADS colons, Windows device
aliases, trailing spaces/dots or invalid Windows characters fail. After stripping
`dev_flash/`, paths remain relative to the destination. Existing symbolic links
and Windows reparse points in any ancestor fail. All packages/paths are validated
before writing; files are committed individually using QSaveFile. This is not a
whole-install rollback on disk failure and does not defend against a concurrent
process maliciously swapping destination directories during extraction.

Resource limits: PUP/individual decrypted sections 1 GiB; inflated section 1 GiB;
aggregate extracted payload 2 GiB. Extraction is in-memory and may require
substantial RAM. The expected RCO's absence is a stderr warning, not a fatal
layout assumption. Missing packages/empty payload is fatal.

## Verification

Enable `-DXMB_FIRMWARE_BUILD_TESTS=ON`, build `xmb-firmware-tests`, then run it.
Tests use generated **synthetic**, non-firmware keys and data:

- NIST SP800-38A AES-128-CTR and AES-256-CBC encryption/decryption vectors.
- RFC 2202 HMAC-SHA1 vector and the no-key fallback.
- Two-entry PUP (version + update TAR), HMAC verification, wrong-hash rejection,
  malformed overflowing offsets and verified-but-no-packages failure.
- Synthetic SCE with CBC-encrypted metadata info, CTR-encrypted metadata/data,
  three sections and a zlib-compressed inner ustar; exact extracted bytes checked.
- Mixed normal/`../evil` TAR fails before writes; absolute, drive, backslash,
  ADS/device names and symlink entries rejected.

The optional test argument writes a synthetic PUP and synthetic runtime key file
to that external directory for testing the actual application's CLI.
No real PUP was downloaded or decrypted; compatibility with Sony's actual
firmware packages remains unverified.

Also checked locally: the real CLI extracted the synthetic encrypted fixture
(2 entries, 1 package, 7 payload bytes, exit 0). A separate Python-generated
no-package PUP using the gitignored PUP_KEY and Python's independent HMAC-SHA1
passed both entry verifications and exited 2 with the expected no-packages error.
The test target forces the no-key include branch, so its tests never depend on
local firmware keys.
