#include "profiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <utility>

namespace xmb {
namespace {

constexpr qint64 MaximumFileBytes = 1024 * 1024;
constexpr qsizetype MaximumProfiles = 16;

// Count Unicode scalar values rather than UTF-16 storage units, so a single
// supplementary-plane character does not consume two of the sixteen positions.
bool normalizeName(const QString& input, QString& normalized) {
    for (qsizetype i = 0; i < input.size(); ++i) {
        const auto ch = input[i];
        if (ch.isHighSurrogate()) {
            if (i + 1 == input.size() || !input[i + 1].isLowSurrogate()) return false;
            ++i;
            continue;
        }
        if (ch.isLowSurrogate()) return false;
        const auto category = ch.category();
        if (category == QChar::Other_Control || category == QChar::Separator_Line ||
            category == QChar::Separator_Paragraph) return false;
    }
    normalized = input.trimmed();
    qsizetype characters = 0;
    for (qsizetype i = 0; i < normalized.size(); ++i) {
        if (normalized[i].isHighSurrogate()) ++i;
        if (++characters > 16) return false;
    }
    return characters > 0;
}

bool hasOnlyKeys(const QJsonObject& object, const QSet<QString>& allowed) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}

}

Profiles::Profiles(const QString& dataDirectory, QObject* parent)
    : QObject(parent), directory_(QDir(dataDirectory).absolutePath()),
      path_(QDir(directory_).filePath("profiles.json")) {
    qRegisterMetaType<Profile>();
    QFile file(path_);
    const QFileInfo info(path_);
    if (!info.exists() && !info.isSymLink()) {
        // commit() updates memory only after QSaveFile has atomically committed.
        const Profile initial{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("User")};
        commit({initial}, initial.id);
        return;
    }
    const auto preserve = [this](const QString& reason) {
        writable_ = false;
        fail(reason + QStringLiteral(" The existing profiles file has been preserved."));
    };
    if (!file.open(QIODevice::ReadOnly)) {
        preserve(QStringLiteral("Profiles could not be read: %1.").arg(file.errorString()));
        return;
    }
    if (file.size() < 0 || file.size() > MaximumFileBytes) {
        preserve(QStringLiteral("The profiles file exceeds the supported size."));
        return;
    }
    // Keep the read bounded even if another process grows the file after size().
    const auto bytes = file.read(MaximumFileBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > MaximumFileBytes || !file.atEnd()) {
        preserve(QStringLiteral("The profiles file could not be read within its size limit."));
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        preserve(QStringLiteral("The profiles file contains invalid JSON."));
        return;
    }
    const auto root = document.object();
    const auto version = root.value("version");
    if (!version.isDouble() || version.toDouble() != 1.0 ||
        !hasOnlyKeys(root, {"version", "profiles", "activeId"}) ||
        !root.value("profiles").isArray() || !root.value("activeId").isString()) {
        preserve(QStringLiteral("Profiles use an unknown, newer or invalid schema."));
        return;
    }
    const auto stored = root.value("profiles").toArray();
    if (stored.isEmpty() || stored.size() > MaximumProfiles) {
        preserve(QStringLiteral("The profiles file contains an invalid number of users."));
        return;
    }
    QVector<Profile> loaded;
    QSet<QString> ids, names;
    for (const auto& value : stored) {
        const auto object = value.toObject();
        if (!value.isObject() || !hasOnlyKeys(object, {"id", "name"}) ||
            !object.value("id").isString() || !object.value("name").isString()) {
            preserve(QStringLiteral("The profiles file contains an invalid user record."));
            return;
        }
        const auto id = object.value("id").toString(), name = object.value("name").toString();
        const QUuid uuid(id);
        QString normalized;
        if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != id || ids.contains(id) ||
            !normalizeName(name, normalized) || normalized != name || names.contains(name.toCaseFolded())) {
            preserve(QStringLiteral("The profiles file contains invalid or duplicate user identifiers/names."));
            return;
        }
        ids.insert(id);
        names.insert(name.toCaseFolded());
        loaded.push_back({id, name});
    }
    const auto active = root.value("activeId").toString();
    if (!ids.contains(active)) {
        preserve(QStringLiteral("The profiles file selects an unknown user."));
        return;
    }
    profiles_ = std::move(loaded);
    activeId_ = active;
}

QString Profiles::activeName() const {
    for (const auto& profile : profiles_)
        if (profile.id == activeId_) return profile.name;
    return {};
}

bool Profiles::fail(QString message) {
    error_ = std::move(message);
    qWarning().noquote() << "profiles:" << error_;
    emit failed(error_);
    return false;
}

bool Profiles::commit(const QVector<Profile>& profiles, const QString& activeId) {
    if (!writable_) { emit failed(error_); return false; }
    if (!QDir().mkpath(directory_)) return fail(QStringLiteral("The local profiles directory could not be created."));
    QJsonArray users;
    for (const auto& profile : profiles) users.append(QJsonObject{{"id", profile.id}, {"name", profile.name}});
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"profiles", users}, {"activeId", activeId}}).toJson();
    QSaveFile file(path_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Profiles could not be saved: %1").arg(file.errorString()));
    if (file.write(bytes) != bytes.size()) {
        const auto reason = file.errorString();
        file.cancelWriting();
        return fail(QStringLiteral("Profiles could not be fully written: %1").arg(reason));
    }
    if (!file.commit()) return fail(QStringLiteral("Profiles could not be committed: %1").arg(file.errorString()));
    profiles_ = profiles;
    activeId_ = activeId;
    error_.clear();
    emit changed();
    return true;
}

bool Profiles::create(QString name) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!writable_) { emit failed(error_); return false; }
    QString normalized;
    if (!normalizeName(name, normalized))
        return fail(QStringLiteral("Enter a user name of 1 to 16 characters without line breaks or control characters."));
    if (profiles_.size() >= MaximumProfiles)
        return fail(QStringLiteral("Up to 16 local users can be created."));
    const auto foldedName = normalized.toCaseFolded();
    for (const auto& profile : profiles_)
        if (profile.name.toCaseFolded() == foldedName)
            return fail(QStringLiteral("A local user with this name already exists."));
    auto next = profiles_;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    next.push_back({id, std::move(normalized)});
    return commit(next, id);
}

bool Profiles::select(QString id) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!writable_) { emit failed(error_); return false; }
    bool found = false;
    for (const auto& profile : profiles_) if (profile.id == id) { found = true; break; }
    if (!found) return fail(QStringLiteral("The selected local user does not exist."));
    if (activeId_ == id) { error_.clear(); return true; }
    return commit(profiles_, id);
}

}
