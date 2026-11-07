#pragma once
// Stored playlists (audit/FEATURE-STATUS-2026-11-04.md item 1, playlist_plugin's own strings).
//
// The firmware's Music and Photo columns carry a Playlists folder (category_music.xml / category_photo.xml
// #seg_playlist_mgmt, playlist_plugin) with "Create New Playlist" first and the user's playlists after it.
// This store is that list for one user: a JSON file per profile under <data>/playlists/<profile id>.json,
// written atomically (QSaveFile), bounded in size and shape, and never touching the media files it names.
// A playlist holds file paths in order; a path that has gone missing stays listed so the shell can say so.
//
// Limits are this build's own (no firmware value was traced): 100 playlists per user and 1000 items per
// playlist. Reaching them returns the error the shell maps to playlist_plugin's msg_error_create_playlist_over
// and msg_error_add_content_over.
#include <QString>
#include <QStringList>
#include <QVector>

namespace xmb {

struct Playlist {
    QString id;          // a UUID without braces
    QString name;        // 1..64 characters, unique per type (case-folded)
    QString type;        // "music" or "photo"
    QStringList items;   // absolute file paths, in play order
};

class PlaylistStore {
public:
    enum class Result { Ok, InvalidName, DuplicateName, TooManyPlaylists, TooManyItems, NotFound, AlreadyListed, WriteFailed };
    static constexpr int MaximumPlaylists = 100;
    static constexpr int MaximumItems = 1000;

    // Points the store at one user's file and loads it. A missing file is an empty store; an unreadable or
    // invalid one is kept on disk untouched, the store starts empty and read-only, and error() says why.
    void open(const QString& dataDirectory, const QString& profileId);
    // Removes a user's file (that user was deleted). Returns true when nothing is left on disk.
    static bool forget(const QString& dataDirectory, const QString& profileId);

    QVector<Playlist> list(const QString& type) const;
    const Playlist* find(const QString& id) const;
    Result create(const QString& type, const QString& name, QString* id = nullptr);
    Result add(const QString& id, const QString& path);
    Result removeItem(const QString& id, int index);
    Result remove(const QString& id);
    QString error() const { return error_; }
    bool writable() const { return writable_; }

private:
    QString path_;
    QString error_;
    bool writable_ = false;
    QVector<Playlist> playlists_;
    bool save(const QVector<Playlist>& next);
};

}
