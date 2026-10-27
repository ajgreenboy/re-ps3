# Content registry and import

The native content registry is a local, versioned JSON file. It is a source of
catalog entries only: this module does not access the network or launch content.
The shell owns the registry path and refreshes its catalog after a successful
import.

```json
{
  "version": 1,
  "apps": [
    {
      "id": "jellyfin",
      "title": "Jellyfin",
      "kind": "jellyfin",
      "category": "video",
      "url": "http://localhost:8096"
    },
    {
      "id": "my-game",
      "title": "My Game",
      "kind": "game",
      "executable": "C:/Games/MyGame/MyGame.exe",
      "workingDirectory": "C:/Games/MyGame",
      "arguments": [],
      "artwork": "art/my-game.png"
    }
  ]
}
```

Replace the example game executable with a path to an existing `.exe` on
Windows. Relative executable, working-directory, and artwork paths resolve from
the JSON file's directory. The importer stores resolved absolute paths. Imports
require each supplied executable, working directory, and artwork file to exist.
When the registry is loaded later, paths are normalized but missing files are
allowed so a deleted or moved asset does not invalidate the whole catalog.
`category` may be
`game`, `tv`, `music`, `video`, or `network`; it defaults to `game` for games
and `tv` for web or Jellyfin entries. Web entries require an `http` or `https` URL with a
host and no embedded credentials. Web entries are intended to open in the
system browser. Use `kind: "jellyfin"` for native authentication, library browsing and playback; its server URL also rejects query/fragment fields and registrations cannot contain credentials or executable arguments. See [native Jellyfin handoff](JELLYFIN-NATIVE.md).

The document is limited to 1 MiB and 1000 entries. Imports validate the complete
source before writing, merge entries by `id` (source entries replace matches),
and replace the target atomically.

`examples/media-apps.json` seeds media services (Jellyfin, Plex, YouTube, Spotify) with the
native Jellyfin kind plus browser `kind: "web"` entries. How these entries surface inside the XMB, next to the owned Steam
library, is described in [XMB-TOOLING.md](XMB-TOOLING.md).