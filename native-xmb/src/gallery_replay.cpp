#include "gallery_controller.h"
#include "preferences.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

// --gallery-replay (audit/PHOTO-GALLERY-DESIGN.md step G5 verification): the controller driven without GL.
namespace xmb {
namespace {
bool tokenKey(const QString& token, GalleryController::Key* key) {
    static const QHash<QString, GalleryController::Key> keys{{"LEFT", GalleryController::Key::Left}, {"RIGHT", GalleryController::Key::Right},
        {"UP", GalleryController::Key::Up}, {"DOWN", GalleryController::Key::Down}, {"ENTER", GalleryController::Key::Cross},
        {"ESC", GalleryController::Key::Circle}, {"TRIANGLE", GalleryController::Key::Triangle}, {"START", GalleryController::Key::Start},
        {"ZOOMIN", GalleryController::Key::ZoomIn}, {"ZOOMOUT", GalleryController::Key::ZoomOut},
        {"L1", GalleryController::Key::L1}, {"R1", GalleryController::Key::R1}};
    const auto found = keys.constFind(token);
    if (found == keys.constEnd()) return false;
    *key = *found;
    return true;
}
const char* actName(GalleryController::Act act) {
    return act == GalleryController::Act::OpenPhoto ? "open-photo" : act == GalleryController::Act::Slideshow ? "slideshow"
        : act == GalleryController::Act::Quit ? "quit" : "none";
}
// Seconds until the focus point is within 0.05 world units of the focus pile after two Right hops, and until the grid blend
// reaches 1 after Enter, at a given tick rate. Both come from the same controller code, so the numbers must agree across rates.
QJsonObject settleAt(const GalleryParams& params, const QVector<GalleryPhoto>& photos, double hz) {
    GalleryController c;
    QString error;
    c.setup(params, photos, GallerySort::Newest, 16.0 / 9.0, &error);
    const double dt = 1.0 / hz;
    for (int i = 0; i < int(hz * 3.0); ++i) c.tick(dt);   // past the boot
    c.press(GalleryController::Key::Right);
    c.press(GalleryController::Key::Right);
    int ticks = 0;
    const auto distance = [&]() {
        const GalleryPile& p = c.layout().piles[c.focusPile()];
        return std::hypot(c.focusX() - p.x, c.focusZ() - p.z);
    };
    while (distance() > 0.05 && ticks < int(hz * 20)) { c.tick(dt); ++ticks; }
    const double focusSeconds = ticks * dt;
    c.press(GalleryController::Key::Cross);
    ticks = 0;
    while (c.gridAmount() < 0.999 && ticks < int(hz * 20)) { c.tick(dt); ++ticks; }
    const double gridSeconds = ticks * dt;
    return QJsonObject{{"hz", hz}, {"focusSettleSeconds", std::round(focusSeconds * 1000) / 1000},
        {"gridEnterSeconds", std::round(gridSeconds * 1000) / 1000}, {"focusPile", c.focusPile()}};
}
}

int runGalleryReplay(const GalleryReplayRequest& request) {
    QString error;
    const GalleryParams params = GalleryParams::load(request.devFlash, &error);
    if (!params.loaded()) { qCritical().noquote() << error; return 2; }
    GallerySort sort = GallerySort::Newest;
    if (!parseGallerySort(request.sort, &sort)) { qCritical() << "--gallery-sort must be newest, oldest or random"; return 2; }
    QStringList roots = request.roots;
    if (roots.isEmpty()) {
        Preferences preferences(request.dataDir);
        const QString configured = preferences.value(QStringLiteral("library/photo")).toString();
        if (!configured.isEmpty()) roots << configured;
    }
    if (roots.isEmpty()) { qCritical() << "gallery-replay: no photo folder (pass --gallery-root)"; return 2; }
    GalleryScanOptions options;
    options.roots = roots;
    options.limit = std::min(options.limit, std::max(1, params.integer("BOOT_CONTENTS_LIMIT")));
    options.indexCachePath = QDir(request.dataDir).filePath(QStringLiteral("cache/gallery/index.json"));
    const GalleryScan scan = scanGalleryPhotos(options);
    GalleryController c;
    if (!c.setup(params, scan.photos, sort, 16.0 / 9.0, &error)) { qCritical().noquote() << "gallery-replay:" << error; return 2; }
    // The Playlist area's jackets: the photo playlists of the first profile file in <data>/playlists (by name).
    const QStringList playlistFiles = QDir(QDir(request.dataDir).filePath(QStringLiteral("playlists")))
        .entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (!playlistFiles.isEmpty()) {
        PlaylistStore store;
        store.open(request.dataDir, QFileInfo(playlistFiles.first()).completeBaseName());
        c.setPlaylists(store.list(QStringLiteral("photo")));
    }
    const double hz = request.hz > 0 ? request.hz : 60.0;
    const double dt = 1.0 / hz;
    QJsonArray steps;
    steps.push_back(QJsonObject{{"token", "start"}, {"state", c.stateJson()}});
    for (QString token : request.tokens.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        token = token.trimmed();
        GalleryController::Key key;
        QJsonObject step{{"token", token}};
        double waitSeconds = 1.2;
        if (token.startsWith(QLatin1String("wait="))) {
            waitSeconds = token.mid(5).toDouble() / 1000.0;
        } else if (tokenKey(token, &key)) {
            const auto result = c.press(key);
            step.insert("act", actName(result.act));
            if (result.act != GalleryController::Act::None) {
                step.insert("files", result.paths.size());
                step.insert("index", result.index);
                if (!result.paths.isEmpty()) step.insert("file", QFileInfo(result.paths.value(result.index)).fileName());
            }
        } else {
            qWarning().noquote() << "gallery-replay: unknown token" << token;
            step.insert("error", "unknown token");
        }
        for (int i = 0; i < int(waitSeconds * hz); ++i) c.tick(dt);
        step.insert("state", c.stateJson());
        steps.push_back(step);
    }
    QJsonArray rates;
    for (const double rate : {30.0, 60.0, 144.0}) rates.push_back(settleAt(params, scan.photos, rate));
    const QJsonObject out{{"kind", "gallery-replay"}, {"photos", scan.photos.size()}, {"piles", c.layout().piles.size()}, {"hz", hz},
        {"steps", steps}, {"frameRateIndependence", rates}};
    QFile file(request.outputPath);
    if (!file.open(QIODevice::WriteOnly)) { qCritical().noquote() << "gallery-replay:" << file.errorString(); return 2; }
    file.write(QJsonDocument(out).toJson());
    qInfo().noquote() << QStringLiteral("gallery-replay: %1 steps over %2 piles").arg(steps.size()).arg(c.layout().piles.size());
    return 0;
}
}
