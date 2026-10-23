# Native XMB stage screenshots

Captured October 23, 2026 from the native Windows runtime, boot session
`20261023-163845-853`, after the floating-texture upload change. These are original
window captures. Capture metadata is in [manifest.json](screenshots/manifest.json).

## 1. Startup notice

Dirty-shutdown notice before Enter acknowledges it.

![Startup notice](screenshots/01-startup-notice.jpg)

## 2. Filesystem recovery prompt

The firmware offers a filesystem check and restart. Escape selected Back after
this capture. This records the prompt, not execution or completion of a rebuild.

![Filesystem recovery prompt](screenshots/02-filesystem-recovery-prompt.jpg)

## 3. Boot warning

Photosensitivity warning after backing out of the recovery prompt.

![Boot warning](screenshots/03-photosensitivity-notice.jpg)

## 4. Game menu

The Game category reached its populated list. Corrupted Data was selected; no
delete or repair action was taken. Background/category icons remain incomplete.

![Game menu](screenshots/04-game-menu.jpg)

## 5. Video menu

One Left press from Game selected Video. Its menu text is visible; the editor
and media playback were not launched.

![Video menu](screenshots/05-video-menu.jpg)

## 6. Music menu

Another Left press selected Music. This demonstrates category navigation only.

![Music menu](screenshots/06-music-menu.jpg)

## Limits and next capture

The background is black and category icons are incorrect in this build. These
screenshots do not establish exact rendering or long-run stability. Phase 5 media
services and Phase 6 launcher work remain future work; their handoffs define the
evidence to capture as those features are implemented.
