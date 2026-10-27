# Native text-entry continuation — October 27, 2026

The integrated Jellyfin sign-in page uses the shared XMB keyboard. Its former name-field rules truncated inserted values to 32 UTF-16 units, trimmed submitted text, and ignored empty submissions. This changed long credentials and prevented clearing a field. A disposable probe linked to the previous sign-in build reproduced all three failures before changes.

## Implemented behavior

- Service edits preserve whitespace and deliver empty accepted values. Cancel leaves the previous value intact.
- Server addresses allow 2,048 UTF-16 units, usernames 256, and passwords 4,096. Display names retain their 32-unit limit, trimming and non-empty rule.
- Insertion uses available capacity without deleting an existing suffix. An over-limit value already present is preserved when additional input is rejected. Limit handling does not split a surrogate pair.
- Backspace and the keyboard's caret controls move by grapheme boundaries. Physical text accepts supplementary Unicode characters.
- Long keyboard values scroll within the field with the caret. Long service values on menu rows are elided for display; stored values remain intact. Password rendering and the UI signature continue to use bullets.

Changes are confined to `src/view.cpp` and `src/view.h`. No authentication API, stream gateway or stored-credential behavior changes.

## Verification and local evidence

The normal `out/native-xmb/re-ps3-native.exe` was rebuilt with the existing build script. A temporary `/Z7` environment override avoids the sandbox's compiler PDB service issue; project build defaults are unchanged.

Disposable verification under ignored `out/native-textentry/` records:

- `baseline-probe.log`: all three former service-input failures reproduced.
- `fixed-probe.log`: 19 checks passed, covering long values, exact password whitespace, masked rows, clearing, cancellation, bounds, preservation of existing text, supplementary/combining characters and rendered keyboard frames.
- `long-password.png` and `long-url.png`: inspected framebuffer captures with synthetic values.
- `build.log`: integrated Release build and Qt deployment.
- `gates.log`: final existing visual regression gate results. These regression gates do not establish firmware fidelity.

These probes are disposable evidence, not a new maintained test suite. They use synthetic strings and make no authentication request.

## Personal server setup and remaining work

The supplied server's public information endpoint responds over HTTP and reports Jellyfin 12.2.0. HTTPS did not connect during the check. No account credentials were transmitted. Its native service registration is stored only under ignored application data, preserving the source examples as generic templates.

Next, the user signs in inside the rebuilt application. Personal-account authentication, browsing and playback remain unverified until that happens. Earlier real-server direct/HLS evidence used a disposable Jellyfin 12.4 server; see [JELLYFIN-NATIVE.md](JELLYFIN-NATIVE.md).

Jellyfin posters, search, resume presentation, server subtitle/track selection and remembered sign-in remain separate open work. Physical-controller text entry and IME/clipboard support have not been verified by this slice.
