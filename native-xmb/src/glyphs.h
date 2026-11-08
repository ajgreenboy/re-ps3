#pragma once
// Icons this build draws in code for its own service pages (Spotify, Jellyfin). They are not firmware
// textures: the console has no Spotify or Jellyfin. Each is a 256 px square, transparent outside the mark,
// built once and kept. Service marks are drawn from their shapes; no image file is shipped or downloaded.
#include <QImage>
#include <QString>

namespace xmb::glyphs {
const QImage& spotifyLogo();     // green disc, three arcs
const QImage& jellyfinLogo();    // violet-to-blue triangle with the inner triangle cut out
const QImage& likedSongs();      // violet-to-mint tile with a white heart
// White line glyphs in the XMB's manner (near-white stroke over a soft shadow).
const QImage& device(const QString& spotifyType);   // computer / phone / TV / speaker
const QImage& signOut();
const QImage& search();
const QImage& more();
// A Jellyfin library by its CollectionType ("movies", "tvshows", "music", ...); null for unknown kinds.
const QImage& library(const QString& collectionType);
}
