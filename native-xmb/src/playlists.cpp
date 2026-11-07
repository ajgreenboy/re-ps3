#include "playlists.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QtDebug>
#include <algorithm>

namespace xmb {
namespace {
constexpr qint64 MaximumFileBytes = 8 * 1024 * 1024;
constexpr int MaximumNameCharacters = 64;
constexpr int MaximumPathCharacters = 4096;

bool validType(const QString& type) { return type == QLatin1String("music") || type == QLatin1String("photo"); }

bool validName(const QString& name) {
    if (name.isEmpty() || name.size() > MaximumNameCharacters || name.trimmed() != name) return false;
    for (const QChar c : name)
        if (c.category() == QChar::Other_Control || c == QChar::LineSeparator || c == QChar::ParagraphSeparator) return false;
    return true;
}

bool validProfile(const QString& id) {
    if (id.isEmpty() || id.size() > 40) return false;
    for (const QChar c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

QString fileFor(const QString& dataDirectory, const QString& profileId) {
    return QDir(dataDirectory).filePath(QStringLiteral("playlists/") + profileId + QStringLiteral(".json"));
}
}

void PlaylistStore::open(const QString& dataDirectory, const QString& profileId) {
    playlists_.clear();error_.clear();writable_ = false;path_.clear();
    if (!validProfile(profileId)) { error_ = QStringLiteral("No valid user is selected."); return; }
    path_ = fileFor(dataDirectory, profileId);
    QFile file(path_);
    if (!file.exists()) { writable_ = true; return; }
    const auto fail = [this](const QString& reason) {
        error_ = reason + QStringLiteral(" The playlists file has been kept as it is.");
        qWarning().noquote() << "playlists:" << error_;
    };
    if (file.size() > MaximumFileBytes) { fail(QStringLiteral("The playlists file is larger than 8 MB.")); return; }
    if (!file.open(QIODevice::ReadOnly)) { fail(QStringLiteral("The playlists file cannot be read.")); return; }
    const auto document = QJsonDocument::fromJson(file.read(MaximumFileBytes + 1));
    const auto root = document.object();
    if (!document.isObject() || root.value(QStringLiteral("version")).toInt() != 1 || !root.value(QStringLiteral("playlists")).isArray()) {
        fail(QStringLiteral("The playlists file is not a version-1 playlist list."));return;
    }
    const auto stored = root.value(QStringLiteral("playlists")).toArray();
    if (stored.size() > MaximumPlaylists) { fail(QStringLiteral("The playlists file lists too many playlists."));return; }
    QSet<QString> ids, names;
    QVector<Playlist> loaded;
    for (const auto& value : stored) {
        const auto object = value.toObject();
        Playlist playlist;
        playlist.id = object.value(QStringLiteral("id")).toString();
        playlist.name = object.value(QStringLiteral("name")).toString();
        playlist.type = object.value(QStringLiteral("type")).toString();
        const QUuid uuid(playlist.id);
        const QString nameKey = playlist.type + QLatin1Char('/') + playlist.name.toCaseFolded();
        if (!value.isObject() || uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != playlist.id || ids.contains(playlist.id) ||
            !validName(playlist.name) || !validType(playlist.type) || names.contains(nameKey) || !object.value(QStringLiteral("items")).isArray()) {
            fail(QStringLiteral("The playlists file contains an invalid playlist."));return;
        }
        const auto items = object.value(QStringLiteral("items")).toArray();
        if (items.size() > MaximumItems) { fail(QStringLiteral("A stored playlist has too many items."));return; }
        for (const auto& item : items) {
            const QString path = item.toString();
            if (!item.isString() || path.isEmpty() || path.size() > MaximumPathCharacters || !QFileInfo(path).isAbsolute()) {
                fail(QStringLiteral("A stored playlist names an invalid path."));return;
            }
            playlist.items << path;
        }
        ids.insert(playlist.id);names.insert(nameKey);
        loaded.push_back(std::move(playlist));
    }
    playlists_ = std::move(loaded);
    writable_ = true;
}

bool PlaylistStore::forget(const QString& dataDirectory, const QString& profileId) {
    if (!validProfile(profileId)) return false;
    const QString path = fileFor(dataDirectory, profileId);
    return !QFile::exists(path) || QFile::remove(path);
}

QVector<Playlist> PlaylistStore::list(const QString& type) const {
    QVector<Playlist> out;
    for (const auto& playlist : playlists_) if (playlist.type == type) out.push_back(playlist);
    return out;
}

const Playlist* PlaylistStore::find(const QString& id) const {
    for (const auto& playlist : playlists_) if (playlist.id == id) return &playlist;
    return nullptr;
}

bool PlaylistStore::save(const QVector<Playlist>& next) {
    if (!writable_ || path_.isEmpty()) return false;
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) { error_ = QStringLiteral("The playlists folder cannot be created.");return false; }
    QJsonArray array;
    for (const auto& playlist : next)
        array.append(QJsonObject{{"id", playlist.id}, {"name", playlist.name}, {"type", playlist.type},
                                 {"items", QJsonArray::fromStringList(playlist.items)}});
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"playlists", array}}).toJson();
    QSaveFile file(path_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error_ = QStringLiteral("The playlists could not be saved: %1").arg(file.errorString());
        qWarning().noquote() << "playlists:" << error_;
        return false;
    }
    playlists_ = next;error_.clear();
    return true;
}

PlaylistStore::Result PlaylistStore::create(const QString& type, const QString& name, QString* id) {
    const QString trimmed = name.trimmed();
    if (!validType(type) || !validName(trimmed)) return Result::InvalidName;
    int count = 0;
    for (const auto& playlist : playlists_) {
        if (playlist.type == type && playlist.name.toCaseFolded() == trimmed.toCaseFolded()) return Result::DuplicateName;
        ++count;
    }
    if (count >= MaximumPlaylists) return Result::TooManyPlaylists;
    auto next = playlists_;
    Playlist playlist{QUuid::createUuid().toString(QUuid::WithoutBraces), trimmed, type, {}};
    if (id) *id = playlist.id;
    next.push_back(playlist);
    return save(next) ? Result::Ok : Result::WriteFailed;
}

PlaylistStore::Result PlaylistStore::add(const QString& id, const QString& path) {
    if (path.isEmpty() || path.size() > MaximumPathCharacters || !QFileInfo(path).isAbsolute()) return Result::NotFound;
    auto next = playlists_;
    for (auto& playlist : next) {
        if (playlist.id != id) continue;
        if (playlist.items.contains(path)) return Result::AlreadyListed;
        if (playlist.items.size() >= MaximumItems) return Result::TooManyItems;
        playlist.items << path;
        return save(next) ? Result::Ok : Result::WriteFailed;
    }
    return Result::NotFound;
}

PlaylistStore::Result PlaylistStore::removeItem(const QString& id, int index) {
    auto next = playlists_;
    for (auto& playlist : next) {
        if (playlist.id != id) continue;
        if (index < 0 || index >= playlist.items.size()) return Result::NotFound;
        playlist.items.removeAt(index);
        return save(next) ? Result::Ok : Result::WriteFailed;
    }
    return Result::NotFound;
}

PlaylistStore::Result PlaylistStore::remove(const QString& id) {
    auto next = playlists_;
    const auto before = next.size();
    next.erase(std::remove_if(next.begin(), next.end(), [&](const Playlist& p) { return p.id == id; }), next.end());
    if (next.size() == before) return Result::NotFound;
    return save(next) ? Result::Ok : Result::WriteFailed;
}

}
