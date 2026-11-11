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
    QVector<ReleaseSource> listSources(QString* error=nullptr) const;
    QString downloadRelease(const QString& source,const QString& title,QString* error=nullptr,
                            const ReleaseProgress& progress={}) const;
private:
    QString dataDirectory_;
    const Preferences& preferences_;
};
}
