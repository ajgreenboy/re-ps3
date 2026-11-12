#pragma once
#include "release_index.h"
#include <QUrl>
#include <functional>

namespace xmb {
class Preferences;
struct ReleaseSource {
    QString slug;
    QString name;
    QUrl url;
    QVector<IndexedRelease> releases;
};
using ReleaseProgress=std::function<void(qint64 received,qint64 total)>;
// Blocking store/network facade. UI callers should run operations on a worker, as for firmware fetch.
// A successful download returns its local path; failures return an empty string and an error.
class ReleaseSources {
public:
    ReleaseSources(const QString& dataDirectory,const Preferences& preferences);
    bool importSource(const QUrl& jsonUrl,QString* error=nullptr);
    bool removeSource(const QString& slug,QString* error=nullptr);
    QVector<ReleaseSource> listSources(QString* error=nullptr) const;
    QString downloadedPath(const QString& source,const QString& title) const;
    QString downloadRelease(const QString& source,const QString& title,QString* error=nullptr,
                            const ReleaseProgress& progress={}) const;
    // Download a game found via the Hydra search: fetch the source index, find the title, resolve the
    // link through the debrid manager, and save the archive into destDir.
    QString downloadGameByUrl(const QString& title,const QUrl& sourceUrl,const QString& destDir,
                              QString* error=nullptr,const ReleaseProgress& progress={}) const;
private:
    bool fetchSourceJson(const QUrl& jsonUrl,QVector<IndexedRelease>* releases,QString* name,QString* error) const;
    QString downloadResolved(const QVector<Download>& candidates,const QString& title,const QString& destDir,
                             QString* error,const ReleaseProgress& progress) const;
    QString dataDirectory_;
    const Preferences& preferences_;
};
}
