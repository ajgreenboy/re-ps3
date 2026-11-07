#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

namespace xmb {

struct Profile {
    QString id;
    QString name;
};

// Local native identity only. Preferences currently remain shared by profiles.
// Call mutations on this object's thread. No firmware or console identity I/O.
class Profiles final : public QObject {
    Q_OBJECT
public:
    explicit Profiles(const QString& dataDirectory, QObject* parent = nullptr);

    QVector<Profile> profiles() const { return profiles_; }
    QString activeId() const { return activeId_; }
    QString activeName() const;
    bool create(QString name);
    bool select(QString id);
    // Renames a user; the name rules and the case-folded duplicate check are create()'s (the firmware's
    // msg_error_other_username_create_pls covers a clash). Renaming to the same name is a no-op success.
    bool rename(QString id, QString name);
    // Deletes a user record. Refused for the active user, an unknown id and the last remaining user; the
    // caller removes that user's settings (Preferences::forgetProfile) once this succeeds.
    bool remove(QString id);
    QString error() const { return error_; }

signals:
    void changed();
    void failed(QString message);

private:
    QString directory_;
    QString path_;
    QString activeId_;
    QString error_;
    QVector<Profile> profiles_;
    bool writable_ = true;

    bool fail(QString message);
    bool commit(const QVector<Profile>& profiles, const QString& activeId);
};

}

Q_DECLARE_METATYPE(xmb::Profile)
