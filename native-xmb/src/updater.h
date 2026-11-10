#pragma once
#include <QDateTime>
#include <QString>
#include <QVector>
#include <functional>

namespace xmb {

struct Asset { QString name, url; qint64 size = 0; };
struct Release {
    QString tag, name, body;
    QVector<Asset> assets;
    QDateTime published;
    QString error;
};

// These synchronous operations should run on a worker thread in the UI.
Release latestRelease(int timeoutMs = 10000);
bool newerVersion(const QString& current, const QString& releaseTag, QString* why = nullptr);
Asset updateAsset(const Release& release);
QString updateHashSource(const Release& release, const Asset& asset);
bool verifyUpdateFile(const QString& path, const QString& expectedHex, QString* error = nullptr);
// Returns a verified temporary ZIP path; the caller owns and eventually removes it.
QString downloadUpdate(const Release& release, QString* error = nullptr,
                       const std::function<void(qint64, qint64)>& progress = {});
// Extracts to a new sibling folder and returns its path; never modifies the installed folder.
bool applyZip(const QString& zipPath, const QString& installDir, QString* stagedPath, QString* error = nullptr);

} // namespace xmb
