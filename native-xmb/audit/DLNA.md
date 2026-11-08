# Search for Media Servers (DLNA / UPnP AV), 2026-11-07

The Video, Music and Photo columns' first row, `msg_scan_mediaserver` ("Search for Media Servers",
category_*.xml `#seg_dlna_scan`), was `Action::Unavailable`. It now runs a real DLNA client.

## What it does

- **Search for Media Servers**: SSDP `M-SEARCH` for `urn:schemas-upnp-org:device:MediaServer:1`, multicast to
  239.255.255.250:1900 from every IPv4 interface, three sends over a 3 s window (`src/dlna_client.*`). Each answer's
  device description gives the server's friendlyName, UDN, ContentDirectory controlURL and icon.
  - Found servers open as a page titled with explore_plugin's `msg_mediaserver` ("Media Server").
  - None found: dlna_plugin's own `msg_error_mediaserver_notfound` ("No media servers were found."), as a message page.
  - More than 64: the first 64 and explore_plugin's `msg_cannot_display_all_mediaserver`.
- **A server or folder**: ContentDirectory `Browse` (BrowseDirectChildren). Folders are listed, then only the column's
  own kind of item: audio for Music, video for Video, images for Photo (by `upnp:class`). Detail line: artist, album,
  duration.
- **Audio / video items** are ordinary File rows whose value is the server's http URL, so the existing queue, player,
  resume points and controls play them (`Shell::startQueuedMedia` takes an http(s) path as a URL; local paths are
  unchanged). They are not local content, so Copy/Delete/Add to Playlist are not offered.
- **Photos** are downloaded (48 MB cap, 20 s timeout, same-origin redirects only), decoded with the same 3840x2160
  bound as local photos, and shown in the photo viewer.
- Errors: explore_plugin's `msg_error_server_transaction_failure` plus the client's reason.

## Servers the multicast cannot reach

A server in a Docker bridge network, or on another subnet, never answers multicast. `<data>/dlna.json` lists them:

    {"locations": ["http://192.168.1.10:8200/rootDesc.xml"], "unicast": ["192.168.1.10"]}

`locations` are fetched directly; `unicast` hosts get a unicast M-SEARCH. Both follow the same rules as an SSDP answer.
`XMB_DLNA_LOCATIONS` (comma-separated) does the same for tests.

## Safety

Description locations must be private, link-local or loopback IP literals; control and icon URLs must be on the
description's host; only http(s) is accepted anywhere (media and album art included); redirects stay same-origin;
responses are capped at 4 MB with a 10 s timeout; malformed XML gives an error or a partial result. No credentials.

## Verification (2026-11-07)

- **Real LAN**: 0 servers. SSDP itself works (the router answers `ssdp:all` with 11 replies), but nothing on this
  network is a MediaServer: Plex's DLNA port 32469 refuses connections (DLNA off or not published from Docker), and
  Jellyfin 12.2's `/dlna/.../description.xml` is 404 (DLNA is a separate plugin since Jellyfin 10.9 and is not
  installed). The XMB shows "No media servers were found."
- **Local mock** (a Bun UPnP MediaServer on 127.0.0.1, `XMB_DLNA_LOCATIONS`): the scan found "Mock Media & Co"
  (embedded MediaServer device, controlURL resolved against URLBase); the root browsed as Music / Videos / Photos;
  Music listed "Track One" (Artist A - Album X - 03:25) and "Track Two <live>", and dropped a track whose media URL was
  `file://`. Standalone client tests also covered SOAP faults (UPnP 701), oversize and stalled responses, non-XML,
  broken DIDL (partial result) and 24 malformed descriptions (no crash).

**Not verified:** a real DLNA server (enable Plex DLNA with ports 32469/tcp and 1900/udp published, or install
Jellyfin's DLNA plugin with host networking), playback of a server stream, and server photos.
