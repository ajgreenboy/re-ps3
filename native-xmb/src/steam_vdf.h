#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace xmb::steamvdf {

// One reader for Valve's KeyValues text ("VDF") shared by the Steam account reader
// (steam_accounts.cpp, config/loginusers.vdf) and the Steam library scanner (steam_library.cpp,
// loginusers.vdf, libraryfolders.vdf, appmanifest_*.acf). It was two private copies; the copies
// differed in the ways listed below, and every difference is an explicit Options field so each
// caller keeps exactly the behaviour it had. tests/steam_vdf_tests.cpp pins that behaviour.
//
// What both copies shared (and this reader still does): whitespace is space, tab, CR, LF, VT, FF;
// "//" starts a comment that runs to the next LF (also inside a bare word, where it ends the word,
// but not inside a quoted string); a quoted string ends at the next unescaped '"', and only \" and
// \\ are escapes (any other backslash is kept literally); an unquoted word runs to whitespace, a
// brace or "//"; strings are UTF-8; a key is followed by a quoted/bare value or a '{' block;
// duplicate keys are all kept in order; any error discards the whole document (no partial tree).
//
// Where the two copies differed (account reader -> library scanner):
//  1. Byte-order mark: the account reader skipped a leading UTF-8 BOM, the library scanner did not
//     (the BOM then became part of the first bare word).                -> Options::skipBom
//  2. Quoted braces: the account reader treated a quoted "{" or "}" as an ordinary string; the
//     library scanner compared token text, so a quoted brace acted as a brace.
//                                                                        -> Options::quotedBracesAreStructural
//  3. Largest token: 4096 bytes (quoted, decoded, and bare) -> 2 MiB (quoted only; a bare word
//     could not exceed the file limit anyway).                           -> Options::maxTokenBytes
//  4. Token count: 20000 -> 100000.                                      -> Options::maxTokens
//  5. Entry count: 4096 -> 20000.                                        -> Options::maxEntries
//  6. Block nesting: 8 -> 32 (a block deeper than the limit is an error). -> Options::maxDepth
//  7. Key lookup: case-sensitive -> case-insensitive.                    -> findEntry's argument
//  8. File reading: the account reader also required the file to be at end-of-file after reading
//     and reported why a read failed; the library scanner did neither.  -> readBounded's arguments
//  9. Error text: the two copies worded their (never shared) messages differently; the library
//     scanner discards the text and only tests for "no error", so the account reader's wording is
//     the one text kept. Unobservable for the library scanner.
// 10. Input ownership: the account reader owned a copy, the library scanner a reference.
//     Unobservable; this reader always works on the caller's bytes.
// The two token-count limits cannot be reached by any input that parses (the account limit of 20000
// exceeds 3 * 4096 + 4096 tokens, the library limit of 100000 exceeds 3 * 20000 + 20000), but they stay
// explicit so nothing relies on that.
struct Options {
    bool skipBom = false;
    bool quotedBracesAreStructural = false;
    qsizetype maxTokenBytes = 4096;
    qsizetype maxTokens = 20000;
    qsizetype maxEntries = 4096;
    int maxDepth = 8;
};

struct Entry {
    QString key;
    QString value;
    QVector<Entry> children;
    bool block = false;
};

struct Document {
    QVector<Entry> entries;
    QString error; // empty on success
};

Document parse(const QByteArray& input, const Options& options);

const Entry* findEntry(const QVector<Entry>& entries, const QString& key, Qt::CaseSensitivity sensitivity);

// The value of a non-block entry, or an empty string.
QString stringValue(const QVector<Entry>& entries, const QString& key, Qt::CaseSensitivity sensitivity);

// Reads a whole file of at most `maximum` bytes. Returns false (and clears `bytes`) when the file
// cannot be opened, is larger than `maximum`, or cannot be read; `detail`, when given, says why.
// With `requireEof` the read must also leave the file at its end (the account reader's stricter
// behaviour; it only differs from a plain read when the file changes while it is being read).
bool readBounded(const QString& path, qsizetype maximum, QByteArray* bytes, QString* detail, bool requireEof);

} // namespace xmb::steamvdf
