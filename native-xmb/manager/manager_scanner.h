#pragma once

// ROM / disc scanning and PARAM.SFO handling for the XMB Manager.
//
// Scanning is read-only with two deliberate exceptions, both inside manager
// controlled caches:
//   - a readable PS3 .pkg's embedded icon may be cached under the system temp
//     directory (xmb-manager-pkg-art) so DetectedGame::artPath names a file;
//   - nothing else is written.
// Encrypted retail packages never contribute metadata that was not readable.

#include "manager_core.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb::manager {

struct DetectedGame {
    QString title;
    QString platform;
    QString romPath;   // absolute, canonical when the filesystem can resolve it
    QString romKind;   // lower-case extension for files, "folder" for EBOOT folders
    qint64 size = 0;   // bytes for files; 0 for folder games (not measured)
    QString artPath;   // absolute path to a readable image, or empty
    QString sourceNote;
};

// Lower-case extensions without the leading dot that scanPaths recognises.
QStringList defaultExtensions();

// Best-effort platform classification. Recognises the extensions below,
// PS3 package folders (EBOOT.BIN / PS3_GAME), ISO9660 system identifiers and
// GameCube/Wii disc magic for .iso files, and platform tokens in the folder
// path. Returns an empty string for an unrecognised path.
//
//   PS3          .pkg .gcm, .iso with PS3VOLUME, folders with EBOOT.BIN
//   PSP          .cso, .iso with PSPGAME
//   PS2          .iso with PLAYSTATION (path hints can select PS1)
//   PS1          .cue/.bin/.chd with a PS1/PSX path hint
//   GameCube/Wii .rvz .wbfs, .iso with GameCube/Wii magic
//   Switch       .nsp .xci
//   3DS          .3ds .cia
//   NDS          .nds
//   SNES         .sfc .smc
//   NES          .nes
//   N64          .n64 .z64 .v64
//   Genesis      .md .gen .smd
//   GBA          .gba
//   Game Boy     .gb
//   Game Boy Color .gbc
//   Saturn       .cue/.bin/.chd with a Saturn path hint
//   Dreamcast    .gdi .cdi, .cue/.bin/.chd with a Dreamcast path hint
//   Arcade       .zip
//   Disc image   .iso .bin .cue .chd .img .mdf that matched no other marker
QString platformForPath(const QString& path);

// Recursive scan. roots may be directories or individual files. Results are
// deduplicated by canonical path and sorted by title then path. Per-path
// problems are appended to warnings; the scan still returns what it found.
QVector<DetectedGame> scanPaths(const QStringList& roots, QStringList* warnings);

// Cleans a file name into a display title: drops the extension, region /
// version / language / dump tags in brackets or parentheses, trailing revision
// markers, and converts underscores to spaces.
QString titleFromFileName(const QString& fileName);

// PARAM.SFO v1.1 reader. Signature 0x46535000 (bytes 00 50 53 46), version
// 0x00000101, all fields little-endian, formats 0x0004 (UTF-8), 0x0204 (UTF-8,
// NUL terminated) and 0x0404 (uint32). Values are returned as strings (uint32
// in decimal).
bool readParamSfo(const QByteArray& data, QHash<QString, QString>& values, QString& error);

// Writes a valid SFO v1.1 byte-exactly: sorted key table, 4-byte aligned data
// table, index entries (key offset, format, length, max length, data offset).
// Every value is written as format 0x0204 (UTF-8, NUL terminated).
bool writeParamSfo(const QHash<QString, QString>& values, QByteArray& data, QString& error);

// Read-only facts from a PS3 .pkg header plus any metadata that is readable
// without decryption. PS3 header layout (big-endian):
//   0x00 magic 0x7F504B47, 0x04 revision u16 (0x8000 finalized/retail,
//   0x0000 non-finalized/debug), 0x06 type u16 (1 = PS3), 0x08 metadata
//   offset u32, 0x0C metadata count u32, 0x10 metadata size u32, 0x14 item
//   count u32, 0x18 total size u64, 0x20 data offset u64, 0x28 data size u64,
//   0x30 content id 0x24 bytes, 0x60 digest 0x10, 0x70 AES-128-CTR IV 0x10,
//   0x80 header digest 0x40 (CMAC + ECDSA + SHA-1 tail).
struct PackageProbe {
    bool pkgMagic = false;
    bool zipContainer = false;
    quint16 revision = 0;
    quint16 type = 0;
    quint32 metadataOffset = 0;
    quint32 metadataCount = 0;
    quint32 metadataSize = 0;
    quint32 itemCount = 0;
    quint64 totalSize = 0;
    quint64 dataOffset = 0;
    quint64 dataSize = 0;
    QString contentId;
    QByteArray digest;
    QByteArray aesIv;
    QByteArray headerDigest;
    bool sfoReadable = false;
    QHash<QString, QString> sfo;
    QByteArray iconPng;      // embedded ICON0 bytes when the container is readable
    QString evidence;        // human-readable header/encryption evidence
};

bool probePs3Package(const QString& path, PackageProbe& probe, QString& error);

// ---------------------------------------------------------------------------
// ROM identity. The manager records a bounded, streamed fingerprint so a moved
// or tampered ROM can be detected and relinked:
//   - files up to 4 GiB: sha256 of the whole file, label "full-sha256-v1";
//   - files over 4 GiB: sha256 over decimal size + first 4 MiB + last 4 MiB,
//     label "partial-sha256-v1";
//   - directories (PS3 package folders): sha256 over the sorted relative file
//     paths and sizes, label "folder-v1".
// Memory use is bounded by a 1 MiB read buffer. mtime is ISO-8601 UTC.
// ---------------------------------------------------------------------------
constexpr qint64 kFullHashLimit = 4LL * 1024 * 1024 * 1024; // 4 GiB
constexpr qint64 kHashChunk = 4 * 1024 * 1024;              // 4 MiB

struct RomFingerprint {
    qint64 size = 0;
    QString mtime;
    QString hashLabel;
    QString hashValue;
};

QString fullHashLabel();
QString partialHashLabel();
QString folderHashLabel();

bool computeRomFingerprint(const QString& path, RomFingerprint& fingerprint, QString& error);

} // namespace xmb::manager
