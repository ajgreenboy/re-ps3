#pragma once

#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb {

struct Profile {
    QString id;
    QString name;
    // The user's icon (user_plugin msg_icon_image_select). Empty: the build's default art
    // (explore_plugin_full:tex_Avatar_Default). "fw:NNN": the firmware's own local-user icon
    // dev_flash/vsh/resource/explore/user/NNN.png, read from the local dev_flash at draw time, never copied.
    // "file": a picture the user chose, square-cropped into <data>/avatars/<id>.png (Profiles::picturePath).
    QString icon;
};

// Local native identity only. Preferences currently remain shared by profiles.
// Call mutations on this object's thread. No firmware or console identity I/O.
class Profiles final : public QObject {
    Q_OBJECT
public:
    explicit Profiles(const QString& dataDirectory, QObject* parent = nullptr);

    QVector<Profile> profiles() const { return profiles_; }
    QString activeId() const { return activeId_; }
    bool create(QString name);
    bool select(QString id);
    // Renames a user; the name rules and the case-folded duplicate check are create()'s (the firmware's
    // msg_error_other_username_create_pls covers a clash). Renaming to the same name is a no-op success.
    bool rename(QString id, QString name);
    // Deletes a user record. Refused for the active user, an unknown id and the last remaining user; the
    // caller removes that user's settings (Preferences::forgetProfile) once this succeeds.
    bool remove(QString id);
    // Sets a user's icon key: "" (default), "fw:NNN" (three digits) or "file" (only once picturePath exists).
    bool setIcon(QString id, QString icon);
    // Copies a picture into the data folder as that user's icon: at most 32 MiB and 8192 px a side, decoded
    // with its orientation applied, centre-cropped to a square and scaled to IconSize, then saved atomically
    // to picturePath(id). On success the user's icon becomes "file".
    bool importPicture(QString id, const QString& sourcePath);
    QString picturePath(const QString& id) const;
    // The firmware's local-user icon files present in a dev_flash (explore/user/NNN.png), as "fw:NNN" keys
    // in file order, and one key's path. Nothing is copied; an absent dev_flash yields an empty list.
    static QStringList firmwareIcons(const QString& flashRoot);
    static QString firmwareIconPath(const QString& flashRoot, const QString& key);
    // The decoded icon for a key (bounded read), or a null image for "" or a missing/unreadable file.
    QImage iconImage(const Profile& profile, const QString& flashRoot) const;
    static QImage readIcon(const QString& path);
    static constexpr int IconSize = 162; // the firmware icons' own size (explore/user/*.png are 162x162)
    // Automatic Login (explore_plugin_full msg_auto_login; the user-row menu's On/Off level, table 0x27dc50).
    // One user at most: "From now on, "%1" will be logged in automatically when the PS3 system is started"
    // (msg_auto_login_set) names a single user, so turning it on for one user turns it off for the others
    // (INFERRED from that wording; the console's storage is not traced).
    // Stored as the optional top-level "autoLogin" key of profiles.json:
    //   absent     a file written before this option existed: the build starts as the last selected user,
    //              exactly as before (legacy; autoLoginId() reports that user so the menu shows On for it);
    //   ""         nobody logs in automatically: with two or more users the XMB starts logged out;
    //   "<id>"     that user is signed in at start.
    QString autoLoginId() const { return autoLoginStored_?autoLogin_:activeId_; }
    bool setAutoLogin(QString id, bool on);
    // The start decision. True: start signed in as startupUser(). False: start logged out (the console's
    // category_user_login.xml column). One user always starts signed in (INFERRED: with one user the
    // firmware's user-row menu has no Log Out/Automatic Login rows, table 0x27dc58).
    bool startsSignedIn() const;
    QString startupUser() const;
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
    QString autoLogin_;            // see autoLoginId(); meaningful only when autoLoginStored_
    bool autoLoginStored_ = false; // the file carries the "autoLogin" key
    bool writable_ = true;

    bool fail(QString message);
    bool commit(const QVector<Profile>& profiles, const QString& activeId);
    bool commit(const QVector<Profile>& profiles, const QString& activeId, bool autoLoginStored, const QString& autoLogin);
};

}

Q_DECLARE_METATYPE(xmb::Profile)
