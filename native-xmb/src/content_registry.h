#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include "display_spec.h"

namespace xmb {

struct ContentApp {
    QString id;
    QString title;
    QString kind;
    QString category;
    QString executable;
    QString workingDirectory;
    QString url;
    QString artwork;
    QString window;   // web apps only: "app" (default, empty = app), "kiosk" or "browser" (P6)
    QStringList arguments;
    DisplaySpec display;   // D9: per-app resolution / refresh / HDR while the title runs (present=false = untouched)
};

class ContentRegistry {
public:
    // Read a version 1 JSON registry. Missing files are treated as an empty registry.
    static bool load(const QString& path, QVector<ContentApp>& apps, QString& error);

    // Validate and merge sourcePath into targetPath by app id, then atomically save it.
    static bool importFile(const QString& sourcePath, const QString& targetPath, QString& error);

    // Validate one app and merge it into the latest registry contents by id.
    static bool upsert(const QString& path, ContentApp app, QString& error);

    // Remove only the registration with id from the latest registry contents.
    static bool remove(const QString& path, const QString& id, QString& error);
};

} // namespace xmb
