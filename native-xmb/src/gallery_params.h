#pragma once
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace xmb {
// The Photo Gallery (hakoniwa, hknw_plugin) parameter table (audit/PHOTO-GALLERY-DESIGN.md step G1).
// The firmware keeps its camera, layout, LOD and colour constants in one preprocessor-style text file,
// vsh/resource/hknw_plugin/parameters.txt: "#define NAME value", Japanese comments after "//". This reads it at run
// time from the local dev_flash; no firmware value is compiled into the program. A name the code needs but the file
// does not carry is not guessed: number() returns 0, the name is recorded in missing(), and callers that cannot work
// without a set of names ask for them up front with require().
//
// Value rules: the first "//" starts a comment ("5.0//4.0" is 5.0); the value is the leading number of what is
// left, so "0.9f" is 0.9 and "0.1/1.0" is 0.1 (the file's own FLOOR_TYPE4_TEXT_G line); anything else is listed in
// rejected() and not stored. A later #define of the same name replaces the earlier one (as the preprocessor would)
// and is listed in duplicates(). The text after "//" is kept per name (comment()) because the thumbnail level sizes
// live there (CACHE_THUMBNAIL_LEVEL1..3: "// 128", "// 256", "// 512").
// Not thread-safe for the missing() bookkeeping: read the values you need once, before sharing across threads.
class GalleryParams final {
public:
    static constexpr const char* RelativePath = "vsh/resource/hknw_plugin/parameters.txt";
    static constexpr qint64 MaximumBytes = 1024 * 1024;

    // dev_flash root -> parsed table; loaded() is false and *error says why when the file is absent or unreadable.
    static GalleryParams load(const QString& devFlash, QString* error = nullptr);
    static GalleryParams parse(const QByteArray& text);

    bool loaded() const { return !values_.isEmpty(); }
    QString source() const { return source_; }
    int count() const { return int(values_.size()); }
    // Number of "#define NAME value" lines seen (including rejected and duplicate ones).
    int defineLines() const { return defineLines_; }
    bool has(const QString& name) const { return values_.contains(name); }

    double number(const char* name) const;
    int integer(const char* name) const;
    // The leading integer of the text after "//" on the name's line, or fallback when there is none.
    int commentInteger(const char* name, int fallback = 0) const;
    QString comment(const char* name) const { return comments_.value(QString::fromLatin1(name)); }

    // Empty when every name is present; otherwise a sentence listing the absent ones.
    QString require(const QStringList& names) const;
    QStringList missing() const { return missing_; }
    QStringList rejected() const { return rejected_; }
    QStringList duplicates() const { return duplicates_; }

    // {source,count,defineLines,rejected,duplicates,missing,values:{NAME:number}} (keys sorted by QJsonObject).
    QJsonObject toJson() const;

private:
    QHash<QString, double> values_;
    QHash<QString, QString> comments_;
    QString source_;
    QStringList rejected_, duplicates_;
    mutable QStringList missing_;
    int defineLines_ = 0;
};
}
