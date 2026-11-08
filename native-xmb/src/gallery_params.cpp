#include "gallery_params.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <algorithm>
#include <charconv>
#include <cmath>

namespace xmb {
namespace {
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
QByteArray trimmed(const QByteArray& text) {
    int begin = 0, end = int(text.size());
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.mid(begin, end - begin);
}
}

GalleryParams GalleryParams::parse(const QByteArray& text) {
    GalleryParams params;
    const auto lines = text.split('\n');
    for (qsizetype number = 0; number < lines.size(); ++number) {
        const QByteArray line = trimmed(lines[number]);
        static const QByteArray define("#define");
        if (!line.startsWith(define) || line.size() == define.size() || !isSpace(line[define.size()])) continue;
        ++params.defineLines_;
        QByteArray rest = trimmed(line.mid(define.size()));
        int nameEnd = 0;
        while (nameEnd < rest.size() && !isSpace(rest[nameEnd])) ++nameEnd;
        const QString name = QString::fromLatin1(rest.left(nameEnd));
        rest = rest.mid(nameEnd);
        QByteArray comment;
        if (const auto slash = rest.indexOf("//"); slash >= 0) {
            comment = trimmed(rest.mid(slash + 2));
            rest = rest.left(slash);
        }
        rest = trimmed(rest);
        if (!rest.isEmpty() && rest[0] == '+') rest.remove(0, 1);
        double value = 0;
        const auto result = std::from_chars(rest.constData(), rest.constData() + rest.size(), value);
        // What may follow the number: nothing, a float suffix ("0.9f") or a second value after a slash ("0.1/1.0").
        const QByteArray tail = result.ec == std::errc() ? rest.mid(int(result.ptr - rest.constData())) : QByteArray("?");
        const bool tailOk = tail.isEmpty() || tail[0] == 'f' || tail[0] == 'F' || tail[0] == '/';
        if (name.isEmpty() || result.ec != std::errc() || !tailOk || !std::isfinite(value)) {
            params.rejected_.push_back(QStringLiteral("%1:%2").arg(number + 1).arg(name));
            continue;
        }
        if (params.values_.contains(name)) params.duplicates_.push_back(name);
        params.values_.insert(name, value);
        params.comments_.insert(name, QString::fromUtf8(comment));
    }
    return params;
}

GalleryParams GalleryParams::load(const QString& devFlash, QString* error) {
    const QString path = QDir(devFlash).filePath(QString::fromLatin1(RelativePath));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumBytes) {
        if (error) *error = QStringLiteral("Photo Gallery parameters not readable: %1").arg(path);
        return {};
    }
    GalleryParams params = parse(file.readAll());
    params.source_ = path;
    if (!params.loaded() && error) *error = QStringLiteral("Photo Gallery parameters hold no #define lines: %1").arg(path);
    return params;
}

double GalleryParams::number(const char* name) const {
    const QString key = QString::fromLatin1(name);
    if (const auto found = values_.constFind(key); found != values_.cend()) return *found;
    if (!missing_.contains(key)) missing_.push_back(key);
    return 0.0;
}

int GalleryParams::integer(const char* name) const { return int(std::lround(number(name))); }

int GalleryParams::commentInteger(const char* name, int fallback) const {
    const QString text = comments_.value(QString::fromLatin1(name)).trimmed();
    int end = 0;
    while (end < text.size() && text[end].isDigit()) ++end;
    bool ok = false;
    const int value = text.left(end).toInt(&ok);
    return ok ? value : fallback;
}

QString GalleryParams::require(const QStringList& names) const {
    QStringList absent;
    for (const QString& name : names) if (!values_.contains(name)) absent.push_back(name);
    if (absent.isEmpty()) return {};
    for (const QString& name : absent) if (!missing_.contains(name)) missing_.push_back(name);
    return QStringLiteral("Photo Gallery parameters lack: %1").arg(absent.join(QStringLiteral(", ")));
}

QJsonObject GalleryParams::toJson() const {
    QJsonObject values;
    for (auto it = values_.cbegin(); it != values_.cend(); ++it) values.insert(it.key(), it.value());
    auto list = [](QStringList names) { std::sort(names.begin(), names.end()); return QJsonArray::fromStringList(names); };
    return {{"source", source_}, {"count", count()}, {"defineLines", defineLines_}, {"rejected", list(rejected_)},
            {"duplicates", list(duplicates_)}, {"missing", list(missing_)}, {"values", values}};
}
}
