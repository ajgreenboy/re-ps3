#pragma once
#include <QString>
namespace xmb {
// Extract an archive into destDir using the system tar (zip/tar) or 7-Zip (rar/7z). Returns false with a
// message on failure. Blocking; call from a worker.
bool extractArchive(const QString& archivePath, const QString& destDir, QString* error=nullptr);
// Breadth-first search for the shallowest game .exe, skipping uninstallers/setup/install stubs.
QString findGameExecutable(const QString& folder, QString* error=nullptr);
} // namespace xmb
