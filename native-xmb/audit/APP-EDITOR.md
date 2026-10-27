# App Editor

The native content-registration editor provides a Qt Widgets window for maintaining the JSON registry used by the XMB content tools.

## Features

- Lists imported game, web app and native Jellyfin registrations and edits their title, kind, category, target, arguments, working directory, and artwork path.
- Adds games by browsing to an executable and uses its filename as the initial title.
- Adds native Jellyfin servers or browser media apps by entering a name and HTTP(S) URL. Jellyfin defaults to TV/Video Services; browser apps offer a category choice.
- Imports existing version 1 JSON registry files through the validated merge-by-ID import path.
- Saves registrations by merging on the stable app ID; IDs remain unchanged while editing.
- Removes only a registry entry. It does not delete app files.
- Uses atomic registry writes and reports parse, validation, and write errors in the window.

## Validation and limits

Web targets must use HTTP or HTTPS, include a host, and contain no URL credentials. Game executables, optional working directories, and optional artwork paths must exist when a registration is saved or imported. Registry parsing retains the existing version, field, size, and entry-count checks. Missing registry files load as an empty registry. A malformed current registry is rejected before any write, preserving its contents.

Arguments are entered one per line. The editor does not manage API keys or credentials. The editor is integrated under Game → Import Games and Apps. Saving refreshes the XMB content providers. Save explicitly before switching registrations or closing; unsaved form edits are not retained. Delete is scoped to the registration list, so it does not intercept deletion while editing text.

## Observed 2026-10-26

Release build passed. Computer Use opened the editor, changed an isolated service title to “Jellyfin POC”, and clicked Save registration. The list updated and apps.json retained ID poc-service, category tv and the localhost URL. A disposable native probe passed add/edit/preserve/remove, invalid URL/credentials/NUL and malformed-existing-file preservation checks. Local evidence: out/native-reference/phase-continuation/. No user registry or service credentials were modified.

## Focused review follow-up

A lower-cost source review found that removing a new unsaved row incorrectly tried the disk registry. The editor now tracks unsaved IDs and removes draft rows locally. A disposable QApplication component replay passed six checks: add draft without disk write; remove draft without disk write; no removal error; add/save new service; generated stable ID; remove saved service through the editor. Evidence: editor-probe.log under the local phase-continuation directory.

Native Jellyfin setup and playback verification: [JELLYFIN-NATIVE.md](JELLYFIN-NATIVE.md). The editor stores only registration metadata; sign-in happens in a separate local dialog.
