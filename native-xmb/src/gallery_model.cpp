#include "gallery_model.h"
#include "gallery_thumbs.h"
#include "preferences.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace xmb {
namespace { constexpr double Pi = 3.14159265358979323846; }
// ============================================================ EXIF capture date

namespace {
// Bounded little/big-endian reader over one byte range. Every read checks the range; nothing indexes past it.
struct Tiff {
    const uchar* data = nullptr;
    qint64 size = 0;
    bool little = true;
    bool u16(qint64 offset, quint16* value) const {
        if (offset < 0 || offset > size - 2) return false;
        *value = little ? quint16(data[offset] | data[offset + 1] << 8) : quint16(data[offset] << 8 | data[offset + 1]);
        return true;
    }
    bool u32(qint64 offset, quint32* value) const {
        if (offset < 0 || offset > size - 4) return false;
        const uchar* p = data + offset;
        *value = little ? quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24
                        : quint32(p[3]) | quint32(p[2]) << 8 | quint32(p[1]) << 16 | quint32(p[0]) << 24;
        return true;
    }
};

bool digit(uchar c) { return c >= '0' && c <= '9'; }

// "YYYY:MM:DD HH:MM:SS" (the date separators are sometimes - or /). Exactly 19 characters are read.
bool parseExifStamp(const uchar* s, QDateTime* out) {
    static const int digits[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
    for (int index : digits) if (!digit(s[index])) return false;
    const auto sep = [](uchar c) { return c == ':' || c == '-' || c == '/'; };
    if (!sep(s[4]) || !sep(s[7]) || !(s[10] == ' ' || s[10] == 'T') || s[13] != ':' || s[16] != ':') return false;
    auto two = [&](int i) { return (s[i] - '0') * 10 + (s[i + 1] - '0'); };
    const int year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    const QDate date(year, two(5), two(8));
    const QTime time(two(11), two(14), two(17));
    if (year < 1900 || year > 2100 || !date.isValid() || !time.isValid()) return false;
    *out = QDateTime(date, time, QTimeZone::UTC);
    return true;
}

struct ExifDates { QDateTime original, digitized, modified; };

// One IFD: collects the three date tags, and the Exif sub-IFD pointer when asked.
bool scanIfd(const Tiff& tiff, quint32 offset, ExifDates* dates, quint32* exifPointer) {
    quint16 count = 0;
    if (!tiff.u16(offset, &count) || count == 0 || count > 1024) return false;
    for (quint32 i = 0; i < count; ++i) {
        const qint64 entry = qint64(offset) + 2 + qint64(i) * 12;
        quint16 tag = 0, type = 0;
        quint32 n = 0;
        if (!tiff.u16(entry, &tag) || !tiff.u16(entry + 2, &type) || !tiff.u32(entry + 4, &n)) return false;
        if (tag == 0x8769 && type == 4 && n == 1 && exifPointer) { tiff.u32(entry + 8, exifPointer); continue; }
        if ((tag != 0x9003 && tag != 0x9004 && tag != 0x0132) || type != 2 || n < 19 || n > 64) continue;
        quint32 valueOffset = 0;
        const qint64 position = n > 4 ? (tiff.u32(entry + 8, &valueOffset) ? qint64(valueOffset) : -1) : entry + 8;
        if (position < 0 || position > tiff.size - 19) continue;
        QDateTime stamp;
        if (!parseExifStamp(tiff.data + position, &stamp)) continue;
        QDateTime& slot = tag == 0x9003 ? dates->original : tag == 0x9004 ? dates->digitized : dates->modified;
        if (!slot.isValid()) slot = stamp;
    }
    return true;
}

bool parseTiffBlock(const uchar* data, qint64 size, QDateTime* taken) {
    if (size < 8) return false;
    Tiff tiff{data, size, data[0] == 'I' && data[1] == 'I'};
    if (!((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M'))) return false;
    quint16 magic = 0;
    quint32 first = 0;
    if (!tiff.u16(2, &magic) || magic != 42 || !tiff.u32(4, &first) || first < 8) return false;
    ExifDates dates;
    quint32 exifPointer = 0;
    if (!scanIfd(tiff, first, &dates, &exifPointer)) return false;
    if (exifPointer >= 8) {
        ExifDates sub;
        if (scanIfd(tiff, exifPointer, &sub, nullptr)) {
            if (sub.original.isValid()) dates.original = sub.original;
            if (sub.digitized.isValid()) dates.digitized = sub.digitized;
        }
    }
    const QDateTime* best = dates.original.isValid() ? &dates.original : dates.digitized.isValid() ? &dates.digitized
                          : dates.modified.isValid() ? &dates.modified : nullptr;
    if (!best) return false;
    *taken = *best;
    return true;
}
}

bool parseJpegCaptureDate(const QByteArray& head, QDateTime* taken) {
    const auto* d = reinterpret_cast<const uchar*>(head.constData());
    const qint64 n = std::min<qint64>(head.size(), GalleryExifHeadBytes);
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    qint64 i = 2;
    for (int guard = 0; guard < 256 && i + 4 <= n; ++guard) {
        if (d[i] != 0xFF) return false;
        while (i < n && d[i] == 0xFF) ++i;
        if (i >= n) return false;
        const uchar marker = d[i++];
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue;  // standalone markers
        if (marker == 0xD9 || marker == 0xDA) return false;                    // end of image / start of scan
        if (i + 2 > n) return false;
        const qint64 length = d[i] << 8 | d[i + 1];
        if (length < 2 || i + length > n) return false;
        if (marker == 0xE1 && length >= 2 + 6 + 8 && std::memcmp(d + i + 2, "Exif\0\0", 6) == 0
            && parseTiffBlock(d + i + 8, length - 8, taken)) return true;
        i += length;
    }
    return false;
}

bool readJpegCaptureDate(const QString& path, QDateTime* taken) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    return parseJpegCaptureDate(file.read(GalleryExifHeadBytes), taken);
}

// ============================================================ index scan

namespace {
bool isPhotoSuffix(const QString& suffix) {
    static const QStringList suffixes{"png", "jpg", "jpeg", "bmp", "webp", "gif", "tif", "tiff"};  // MediaLibrary's photo set
    return suffixes.contains(suffix.toLower());
}
bool isJpegSuffix(const QString& suffix) { const QString s = suffix.toLower(); return s == QLatin1String("jpg") || s == QLatin1String("jpeg"); }

struct IndexEntry { qint64 size = 0, modifiedMs = 0; QDateTime taken; bool exif = false; };
constexpr qint64 MaximumIndexBytes = 64ll * 1024 * 1024;
constexpr char StampFormat[] = "yyyy-MM-ddTHH:mm:ss";

QHash<QString, IndexEntry> loadIndex(const QString& path) {
    QHash<QString, IndexEntry> out;
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > MaximumIndexBytes) return out;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || doc.object().value("version").toInt() != 1) return out;
    for (const QJsonValue& value : doc.object().value("entries").toArray()) {
        const QJsonObject o = value.toObject();
        QDateTime taken = QDateTime::fromString(o.value("t").toString(), QString::fromLatin1(StampFormat));
        if (!taken.isValid() || o.value("p").toString().isEmpty()) continue;
        taken.setTimeZone(QTimeZone::UTC);
        out.insert(o.value("p").toString(), {qint64(o.value("s").toDouble()), qint64(o.value("m").toDouble()), taken, o.value("e").toBool()});
    }
    return out;
}

void saveIndex(const QString& path, const QVector<GalleryPhoto>& photos) {
    QJsonArray entries;
    for (const GalleryPhoto& p : photos)
        entries.push_back(QJsonObject{{"p", p.path}, {"s", double(p.size)}, {"m", double(p.modifiedMs)},
                                      {"t", p.taken.toString(QString::fromLatin1(StampFormat))}, {"e", p.exif}});
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) return;
    out.write(QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}}).toJson(QJsonDocument::Compact));
    out.commit();
}
}

GalleryScan scanGalleryPhotos(const GalleryScanOptions& options, const std::atomic_bool* cancel) {
    GalleryScan scan;
    const auto index = loadIndex(options.indexCachePath);
    QSet<QString> seen;
    bool dirty = false;
    struct Pending { QString directory; int depth; };
    for (const QString& rootPath : options.roots) {
        const QString root = QFileInfo(rootPath).canonicalFilePath();
        if (root.isEmpty() || !QFileInfo(root).isDir()) {
            if (scan.error.isEmpty()) scan.error = QStringLiteral("Photo folder not found: %1").arg(rootPath);
            continue;
        }
        QVector<Pending> stack{{root, 0}};
        while (!stack.isEmpty() && !scan.truncated) {
            if (cancel && cancel->load()) { scan.cancelled = true; return scan; }
            const Pending current = stack.takeLast();
            if (++scan.directories > options.maxDirectories) { scan.truncated = true; break; }
            const QFileInfoList entries = QDir(current.directory).entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::DirsLast);
            QVector<QString> subdirectories;
            for (const QFileInfo& info : entries) {
                if (info.isDir()) { if (current.depth < options.maxDepth) subdirectories.push_back(info.filePath()); continue; }
                if (!isPhotoSuffix(info.suffix()) || info.size() <= 0) continue;
                const QString path = info.filePath();
                if (seen.contains(path)) continue;
                if (scan.photos.size() >= options.limit) { scan.truncated = true; break; }
                seen.insert(path);
                GalleryPhoto photo;
                photo.path = path;
                photo.size = info.size();
                photo.modifiedMs = info.lastModified().toMSecsSinceEpoch();
                const auto cached = index.constFind(path);
                if (cached != index.cend() && cached->size == photo.size && cached->modifiedMs == photo.modifiedMs) {
                    photo.taken = cached->taken;
                    photo.exif = cached->exif;
                    ++scan.reusedFromIndex;
                } else {
                    dirty = true;
                    if (isJpegSuffix(info.suffix()) && readJpegCaptureDate(path, &photo.taken)) {
                        photo.exif = true;
                    } else {
                        const QDateTime local = info.lastModified().toLocalTime();
                        photo.taken = QDateTime(local.date(), local.time(), QTimeZone::UTC);
                    }
                }
                photo.exif ? ++scan.exifRead : ++scan.fileTimeUsed;
                scan.photos.push_back(photo);
            }
            for (int i = int(subdirectories.size()) - 1; i >= 0; --i) stack.push_back({subdirectories[i], current.depth + 1});
        }
    }
    std::sort(scan.photos.begin(), scan.photos.end(), [](const GalleryPhoto& a, const GalleryPhoto& b) {
        return a.taken != b.taken ? a.taken < b.taken : a.path < b.path; });
    if (!options.indexCachePath.isEmpty() && (dirty || index.size() != scan.photos.size())) saveIndex(options.indexCachePath, scan.photos);
    return scan;
}

GalleryIndexer::GalleryIndexer(QObject* parent) : QObject(parent) {
    qRegisterMetaType<xmb::GalleryScan>();
}
GalleryIndexer::~GalleryIndexer() { cancel(); }
void GalleryIndexer::cancel() {
    if (cancel_) cancel_->store(true);
    activeId_ = 0;
}
quint64 GalleryIndexer::start(const GalleryScanOptions& options) {
    cancel();
    const quint64 id = ++nextId_;
    activeId_ = id;
    auto flag = std::make_shared<std::atomic_bool>(false);
    cancel_ = flag;
    QPointer<GalleryIndexer> self(this);
    QThreadPool::globalInstance()->start([self, id, options, flag] {
        GalleryScan scan = scanGalleryPhotos(options, flag.get());
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, id, scan = std::move(scan)]() mutable {
            if (self && id == self->activeId_) emit self->finished(id, std::move(scan));
        }, Qt::QueuedConnection);
    });
    return id;
}

// ============================================================ layout

QString gallerySortName(GallerySort sort) {
    return sort == GallerySort::Newest ? QStringLiteral("newest") : sort == GallerySort::Oldest ? QStringLiteral("oldest") : QStringLiteral("random");
}
bool parseGallerySort(const QString& text, GallerySort* sort) {
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("newest")) *sort = GallerySort::Newest;
    else if (t == QLatin1String("oldest")) *sort = GallerySort::Oldest;
    else if (t == QLatin1String("random")) *sort = GallerySort::Random;
    else return false;
    return true;
}

QStringList galleryLayoutParamNames() {
    return {"PARAM_MIN_DX", "PARAM_MIN_DZ", "PARAM_UNIT_DX", "PARAM_UNIT_DZ", "PARAM_MAX_RANDOM_DX", "PARAM_MAX_RANDOM_DZ",
            "PARAM_LINE_DZ_STEP", "PARAM_MAX_PER_UNIT_DX", "PARAM_DISP_YEAR_PER_UNIT_DX", "PARAM_MAX_LINE_LENGTH",
            "PARAM_LINE_GRAD_MIN", "PARAM_LINE_GRAD_MAX", "PARAM_CURVE", "PARAM_CURVE_POINT",
            "PARAM_PHOTO_VIEW_COUNT_X1", "PARAM_PHOTO_VIEW_COUNT_X2", "PARAM_PHOTO_VIEW_COUNT_X3",
            "PARAM_PHOTO_VIEW_COUNT_Y1", "PARAM_PHOTO_VIEW_COUNT_Y2", "PARAM_PHOTO_VIEW_COUNT_Y3",
            "GROUP_WIDTH_RATIO", "GROUP_HEIGHT_RATIO", "GROUP_SCALE_OFFSET_BY_LIMIT", "GROUP_RANGE_ROTATION_Y",
            "GROUP_RANGE_MUTANT_RATE", "GROUP_RANGE_MUTANT_ROTATION_Y", "CAMERA_FAR_LIMIT"};
}

namespace {
// splitmix64: a fixed, portable generator so a pile scatters the same way on every machine and build.
struct Rng {
    quint64 state;
    quint64 next() {
        state += 0x9E3779B97F4A7C15ull;
        quint64 z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double lo, double hi) { return lo + (hi - lo) * unit(); }
};

double round4(double v) { return std::round(v * 10000.0) / 10000.0; }

struct LayoutConstants {
    double minDx, minDz, unitDx, unitDz, randomDx, randomDz, lineDzStep, maxPerUnitDx, yearPerUnitDx, maxLineLength;
    double gradMin, gradMax, curve, curvePoint;
    double x[3], y[3];
    double widthRatio, heightRatio, scaleOffset, rotationY, mutantRate, mutantRotationY, farLimit;
};

LayoutConstants constantsFrom(const GalleryParams& p) {
    LayoutConstants c{};
    c.minDx = p.number("PARAM_MIN_DX"); c.minDz = p.number("PARAM_MIN_DZ");
    c.unitDx = p.number("PARAM_UNIT_DX"); c.unitDz = p.number("PARAM_UNIT_DZ");
    c.randomDx = p.number("PARAM_MAX_RANDOM_DX"); c.randomDz = p.number("PARAM_MAX_RANDOM_DZ");
    c.lineDzStep = p.number("PARAM_LINE_DZ_STEP"); c.maxPerUnitDx = p.number("PARAM_MAX_PER_UNIT_DX");
    c.yearPerUnitDx = p.number("PARAM_DISP_YEAR_PER_UNIT_DX"); c.maxLineLength = p.number("PARAM_MAX_LINE_LENGTH");
    c.gradMin = p.number("PARAM_LINE_GRAD_MIN"); c.gradMax = p.number("PARAM_LINE_GRAD_MAX");
    c.curve = p.number("PARAM_CURVE"); c.curvePoint = p.number("PARAM_CURVE_POINT");
    c.x[0] = p.number("PARAM_PHOTO_VIEW_COUNT_X1"); c.x[1] = p.number("PARAM_PHOTO_VIEW_COUNT_X2"); c.x[2] = p.number("PARAM_PHOTO_VIEW_COUNT_X3");
    c.y[0] = p.number("PARAM_PHOTO_VIEW_COUNT_Y1"); c.y[1] = p.number("PARAM_PHOTO_VIEW_COUNT_Y2"); c.y[2] = p.number("PARAM_PHOTO_VIEW_COUNT_Y3");
    c.widthRatio = p.number("GROUP_WIDTH_RATIO"); c.heightRatio = p.number("GROUP_HEIGHT_RATIO");
    c.scaleOffset = p.number("GROUP_SCALE_OFFSET_BY_LIMIT"); c.rotationY = p.number("GROUP_RANGE_ROTATION_Y");
    c.mutantRate = p.number("GROUP_RANGE_MUTANT_RATE"); c.mutantRotationY = p.number("GROUP_RANGE_MUTANT_ROTATION_Y");
    c.farLimit = p.number("CAMERA_FAR_LIMIT");
    return c;
}

// INFERRED: PARAM_PHOTO_VIEW_COUNT_Xn / _Yn read as breakpoints "a pile of Xn photos shows Yn": linear between
// (0,0), (X1,Y1), (X2,Y2), (X3,Y3), flat at Y3 beyond X3. The module's own routine was not traced (G2b).
int visibleCount(const LayoutConstants& c, int photos) {
    if (photos <= 0) return 0;
    double px = 0, py = 0, shown = c.y[2];
    bool found = false;
    for (int i = 0; i < 3 && !found; ++i) {
        if (photos <= c.x[i] && c.x[i] > px) { shown = py + (c.y[i] - py) * (photos - px) / (c.x[i] - px); found = true; }
        px = c.x[i]; py = c.y[i];
    }
    return std::clamp(int(std::lround(shown)), 1, photos);
}

// Pile spread. INFERRED from GROUP_WIDTH_RATIO / GROUP_HEIGHT_RATIO ("the spread of a group's width/height") applied
// to a photo footprint of PARAM_MIN_DX x PARAM_MIN_DZ (the spacing the module keeps between photos): a single photo
// has half the footprint, a full pile approaches half the footprint times the ratio.
void pileExtent(const LayoutConstants& c, int visible, int photos, double* halfWidth, double* halfDepth) {
    const double fill = 1.0 - 1.0 / std::sqrt(double(std::max(1, visible)));
    double w = 0.5 * c.minDx * (1.0 + (c.widthRatio - 1.0) * fill);
    double d = 0.5 * c.minDz * (1.0 + (c.heightRatio - 1.0) * fill);
    if (photos > c.x[2] && c.scaleOffset > 0) { const double extra = 1.0 + c.scaleOffset * (photos - c.x[2]); w *= extra; d *= extra; }
    *halfWidth = w;
    *halfDepth = d;
}
}

GalleryLayout layoutGallery(const QVector<GalleryPhoto>& photos, const GalleryParams& params, const GalleryLayoutOptions& options) {
    GalleryLayout out;
    out.sort = options.sort;
    if (const QString missing = params.require(galleryLayoutParamNames()); !missing.isEmpty()) { out.error = missing; return out; }
    const LayoutConstants c = constantsFrom(params);
    if (!(c.minDx > 0 && c.minDz > 0 && c.maxLineLength > 0 && c.farLimit > 0)) { out.error = QStringLiteral("Photo Gallery layout parameters are not positive"); return out; }
    if (photos.isEmpty()) { out.ok = true; return out; }

    // Chronological order, then calendar-day piles (INFERRED: the module's "time slot" group is a day here).
    QVector<int> order(photos.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return photos[a].taken != photos[b].taken ? photos[a].taken < photos[b].taken : photos[a].path < photos[b].path; });
    QVector<GalleryPile> piles;
    for (int index : std::as_const(order)) {
        const QDate date = photos[index].taken.date();
        if (piles.isEmpty() || piles.last().date != date) {
            GalleryPile pile;
            pile.date = date;
            pile.id = date.toJulianDay() - QDate(1970, 1, 1).toJulianDay();
            pile.label = date.toString(QStringLiteral("yyyy/MM/dd"));
            piles.push_back(pile);
        }
        piles.last().photos.push_back(index);
    }
    if (options.sort == GallerySort::Newest) {
        std::reverse(piles.begin(), piles.end());
        for (GalleryPile& pile : piles) std::reverse(pile.photos.begin(), pile.photos.end());
    } else if (options.sort == GallerySort::Random) {
        Rng rng{options.seed};
        for (int i = int(piles.size()) - 1; i > 0; --i) std::swap(piles[i], piles[int(rng.next() % quint64(i + 1))]);
    }

    for (GalleryPile& pile : piles) {
        pile.visible = visibleCount(c, int(pile.photos.size()));
        pileExtent(c, pile.visible, int(pile.photos.size()), &pile.halfWidth, &pile.halfDepth);
    }

    // Placement lines. INFERRED shape: piles advance along a line of at most PARAM_MAX_LINE_LENGTH x the screen width
    // (measured at CAMERA_FAR_LIMIT with the options' field of view); the line leaves at PARAM_LINE_GRAD_MIN degrees
    // and, after PARAM_CURVE_POINT of its length, bends towards PARAM_LINE_GRAD_MAX in proportion to PARAM_CURVE
    // (1.0 = fully). The next line starts PARAM_LINE_DZ_STEP further along z.
    const double viewWidth = 2.0 * c.farLimit * std::tan(options.fovYDegrees * Pi / 360.0) * options.aspect;
    out.lineLength = c.maxLineLength * viewWidth;
    const double stepLow = c.minDx, stepHigh = std::max(c.minDx, c.maxPerUnitDx * c.unitDx);
    const double bend = std::clamp(c.curve, 0.0, 1.0);
    double pathX = 0, pathZ = 0, along = 0;
    int row = 0, inRow = 0;
    const auto advance = [&](double distance) {
        constexpr double Sub = 0.25;
        for (double left = distance; left > 1e-9;) {
            const double ds = std::min(Sub, left);
            const double t = std::clamp(along / out.lineLength, 0.0, 1.0);
            const double u = c.curvePoint >= 1.0 ? 0.0 : std::clamp((t - c.curvePoint) / (1.0 - c.curvePoint), 0.0, 1.0);
            const double angle = (c.gradMin + (c.gradMax - c.gradMin) * bend * u) * Pi / 180.0;
            pathX += ds * std::cos(angle);
            pathZ += ds * std::sin(angle);
            along += ds;
            left -= ds;
        }
    };
    for (int k = 0; k < piles.size(); ++k) {
        GalleryPile& pile = piles[k];
        if (k > 0) {
            const GalleryPile& previous = piles[k - 1];
            const double gapDays = std::abs(double(pile.date.toJulianDay() - previous.date.toJulianDay()));
            double step = std::clamp(gapDays * c.unitDx, stepLow, stepHigh);
            if (pile.date.year() != previous.date.year()) {
                pile.yearBreakBefore = pile.date.year();
                step = std::max(step, c.yearPerUnitDx * c.unitDx);
            }
            step = std::max(step, options.pileClearance * (previous.halfWidth + pile.halfWidth));
            if (inRow > 0 && along + step > out.lineLength) {
                ++row; inRow = 0; along = 0; pathX = 0; pathZ = row * c.lineDzStep;
            } else {
                advance(step);
                pathZ += gapDays * c.unitDz;
            }
        }
        Rng rng{quint64(pile.id) + 0x51ED270Bull};
        pile.row = row;
        pile.x = pathX + (c.randomDx > 0 ? rng.range(-c.randomDx, c.randomDx) : 0.0);
        pile.z = pathZ + (c.randomDz > 0 ? rng.range(-c.randomDz, c.randomDz) : 0.0);
        pile.colorIndex = k % 6;  // BORDER_LINE_COLOR_0..5 / GROUP_TEXT_COLOR_0..5
        ++inRow;
    }
    out.rows = row + 1;

    // Centre the pile centres on the origin.
    double minX = 1e300, maxX = -1e300, minZ = 1e300, maxZ = -1e300;
    for (const GalleryPile& pile : std::as_const(piles)) {
        minX = std::min(minX, pile.x); maxX = std::max(maxX, pile.x);
        minZ = std::min(minZ, pile.z); maxZ = std::max(maxZ, pile.z);
    }
    for (GalleryPile& pile : piles) { pile.x -= (minX + maxX) / 2; pile.z -= (minZ + maxZ) / 2; }

    // Photo scatter: an ellipse of the pile's half extents, seeded by the pile id. The visible photos are spread
    // evenly through the pile's time order; the rest stay under the pile and appear in the grid.
    for (GalleryPile& pile : piles) {
        Rng rng{quint64(pile.id) * 0x9E3779B97F4A7C15ull + 1};
        const int n = int(pile.photos.size());
        for (int k = 0; k < pile.visible; ++k) {
            const int at = pile.visible == 1 ? n / 2 : int(std::lround(double(k) * (n - 1) / (pile.visible - 1)));
            const double radius = std::sqrt(rng.unit()), angle = rng.range(0, 2 * Pi);
            const bool mutant = rng.unit() < c.mutantRate;
            const double spread = c.rotationY + (mutant ? c.mutantRotationY : 0.0);
            GalleryPlacedPhoto placed;
            placed.photo = pile.photos[at];
            placed.x = pile.x + pile.halfWidth * radius * std::cos(angle);
            placed.z = pile.z + pile.halfDepth * radius * std::sin(angle);
            placed.rotationY = rng.range(-spread, spread);
            pile.shown.push_back(placed);
        }
    }

    // Closest pair of centres (sweep over x).
    out.minCentreDistance = 0;
    if (piles.size() > 1) {
        QVector<int> byX(piles.size());
        std::iota(byX.begin(), byX.end(), 0);
        std::sort(byX.begin(), byX.end(), [&](int a, int b) { return piles[a].x < piles[b].x; });
        double best = 1e300;
        for (int i = 0; i < byX.size(); ++i)
            for (int j = i + 1; j < byX.size() && piles[byX[j]].x - piles[byX[i]].x < best; ++j)
                best = std::min(best, std::hypot(piles[byX[j]].x - piles[byX[i]].x, piles[byX[j]].z - piles[byX[i]].z));
        out.minCentreDistance = best;
    }
    out.piles = piles;
    out.ok = true;
    return out;
}

QVector<GalleryGridCell> layoutGalleryGrid(const GalleryPile& pile, const GalleryParams& params, double aspect) {
    QVector<GalleryGridCell> cells;
    const double dx = params.number("PARAM_MIN_DX"), dz = params.number("PARAM_MIN_DZ");
    const int n = int(pile.photos.size());
    if (n == 0 || dx <= 0 || dz <= 0) return cells;
    const int columns = std::max(1, int(std::ceil(std::sqrt(n * aspect * dz / dx))));
    const int rows = (n + columns - 1) / columns;
    for (int i = 0; i < n; ++i) {
        cells.push_back({pile.photos[i], (i % columns - (columns - 1) / 2.0) * dx, (i / columns - (rows - 1) / 2.0) * dz});
    }
    return cells;
}

// ============================================================ report

namespace {
QString relativeTo(const QStringList& roots, const QString& path) {
    for (const QString& root : roots) {
        const QString canonical = QFileInfo(root).canonicalFilePath();
        if (!canonical.isEmpty() && path.startsWith(canonical + QLatin1Char('/'))) return path.mid(canonical.size() + 1);
    }
    return path;
}
bool writeReport(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { qCritical().noquote() << "gallery report:" << file.errorString(); return false; }
    const QByteArray bytes = QJsonDocument(object).toJson();
    return file.write(bytes) == bytes.size();
}
}

int runGalleryReport(const GalleryReportRequest& request) {
    QString error;
    const GalleryParams params = GalleryParams::load(request.devFlash, &error);
    if (!params.loaded()) { qCritical().noquote() << error; return 2; }
    if (!request.layout) {
        // The parameter table alone. Everything the gallery asks for is also checked, so a file with a renamed
        // constant fails here and not in the scene.
        GalleryThumbs::Policy policy;
        QString thumbError;
        const bool thumbsOk = GalleryThumbs::policyFromParams(params, &policy, &thumbError);
        const QString layoutError = params.require(galleryLayoutParamNames());
        QJsonObject out = params.toJson();
        out.insert("layoutParamsComplete", layoutError.isEmpty());
        out.insert("thumbParamsComplete", thumbsOk);
        if (!layoutError.isEmpty()) out.insert("layoutError", layoutError);
        if (!thumbsOk) out.insert("thumbError", thumbError);
        if (!writeReport(request.outputPath, out)) return 2;
        qInfo().noquote() << QStringLiteral("gallery-params: %1 values from %2 #define lines (%3 rejected, %4 duplicates)")
            .arg(params.count()).arg(params.defineLines()).arg(params.rejected().size()).arg(params.duplicates().size());
        return layoutError.isEmpty() && thumbsOk ? 0 : 1;
    }

    GallerySort sort = GallerySort::Newest;
    if (!parseGallerySort(request.sort, &sort)) { qCritical() << "--gallery-sort must be newest, oldest or random"; return 2; }
    QStringList roots = request.roots;
    if (roots.isEmpty()) {
        Preferences preferences(request.dataDir);
        const QString configured = preferences.value(QStringLiteral("library/photo")).toString();
        if (!configured.isEmpty()) roots << configured;
    }
    if (roots.isEmpty()) { qCritical() << "gallery-layout: no photo folder (pass --gallery-root or set the Photo library folder)"; return 2; }

    GalleryScanOptions scanOptions;
    scanOptions.roots = roots;
    scanOptions.limit = std::min(scanOptions.limit, std::max(1, params.integer("BOOT_CONTENTS_LIMIT")));
    scanOptions.indexCachePath = QDir(request.dataDir).filePath(QStringLiteral("cache/gallery/index.json"));
    const GalleryScan scan = scanGalleryPhotos(scanOptions);

    GalleryLayoutOptions layoutOptions;
    layoutOptions.sort = sort;
    const GalleryLayout layout = layoutGallery(scan.photos, params, layoutOptions);

    QJsonObject out{{"kind", "gallery-layout"}, {"sort", gallerySortName(sort)},
        {"params", QJsonObject{{"source", params.source()}, {"count", params.count()}, {"missing", QJsonArray::fromStringList(params.missing())}}}};
    QJsonArray rootNames;
    for (const QString& root : roots) rootNames.push_back(QFileInfo(root).canonicalFilePath());
    out.insert("roots", rootNames);
    out.insert("scan", QJsonObject{{"photos", scan.photos.size()}, {"limit", scanOptions.limit}, {"truncated", scan.truncated},
        {"directories", scan.directories}, {"exifRead", scan.exifRead}, {"fileTimeUsed", scan.fileTimeUsed},
        {"reusedFromIndex", scan.reusedFromIndex}, {"error", scan.error}});

    QJsonArray piles, years;
    int maxPile = -1;
    for (int i = 0; i < layout.piles.size(); ++i) {
        const GalleryPile& pile = layout.piles[i];
        if (maxPile < 0 || pile.photos.size() > layout.piles[maxPile].photos.size()) maxPile = i;
        QJsonArray shown;
        for (const GalleryPlacedPhoto& placed : pile.shown)
            shown.push_back(QJsonObject{{"file", relativeTo(roots, scan.photos[placed.photo].path)}, {"x", round4(placed.x)},
                                        {"z", round4(placed.z)}, {"rotationY", round4(placed.rotationY)}});
        piles.push_back(QJsonObject{{"id", double(pile.id)}, {"date", pile.date.toString(Qt::ISODate)}, {"label", pile.label},
            {"photos", pile.photos.size()}, {"visible", pile.visible}, {"colorIndex", pile.colorIndex}, {"row", pile.row},
            {"x", round4(pile.x)}, {"z", round4(pile.z)}, {"halfWidth", round4(pile.halfWidth)}, {"halfDepth", round4(pile.halfDepth)},
            {"yearBreakBefore", pile.yearBreakBefore}, {"shown", shown}});
        if (pile.yearBreakBefore) years.push_back(QJsonObject{{"year", pile.yearBreakBefore}, {"beforePile", i}});
    }
    out.insert("layout", QJsonObject{{"ok", layout.ok}, {"error", layout.error}, {"pileCount", layout.piles.size()}, {"rows", layout.rows},
        {"lineLength", round4(layout.lineLength)}, {"minCentreDistance", round4(layout.minCentreDistance)},
        {"minDx", params.number("PARAM_MIN_DX")}, {"centresRespectMinDx", layout.piles.size() < 2 || layout.minCentreDistance >= params.number("PARAM_MIN_DX") - 1e-9},
        {"yearLabels", years}, {"piles", piles}});
    if (maxPile >= 0) {
        const auto cells = layoutGalleryGrid(layout.piles[maxPile], params);
        QJsonArray grid;
        for (const GalleryGridCell& cell : cells)
            grid.push_back(QJsonObject{{"file", relativeTo(roots, scan.photos[cell.photo].path)}, {"x", round4(cell.x)}, {"z", round4(cell.z)}});
        out.insert("gridSample", QJsonObject{{"pile", layout.piles[maxPile].label}, {"cells", grid}});
    }

    int result = layout.ok ? 0 : 2;
    if (request.warmThumbs && layout.ok) {
        GalleryThumbs::Config config;
        QString policyError;
        if (!GalleryThumbs::policyFromParams(params, &config.policy, &policyError)) { qCritical().noquote() << policyError; return 2; }
        config.directory = request.thumbDirOverride.isEmpty() ? QDir(request.dataDir).filePath(QStringLiteral("cache/gallery/thumbs")) : request.thumbDirOverride;
        constexpr int WarmLimit = 2000;
        GalleryThumbs thumbs(config);
        QElapsedTimer timer;
        timer.start();
        QJsonArray levels;
        const int count = std::min<int>(scan.photos.size(), WarmLimit);
        for (int level = GalleryThumbs::Small; level <= GalleryThumbs::Large; ++level) {
            const auto before = thumbs.stats();
            QElapsedTimer levelTimer;
            levelTimer.start();
            for (int i = 0; i < count; ++i) thumbs.request(scan.photos[i].path, GalleryThumbs::Level(level), false);
            thumbs.waitIdle();
            const auto after = thumbs.stats();
            levels.push_back(QJsonObject{{"level", level}, {"side", thumbs.sideOf(GalleryThumbs::Level(level))}, {"requested", count},
                {"diskHits", double(after.diskHits - before.diskHits)}, {"generated", double(after.generated - before.generated)},
                {"failed", double(after.failed - before.failed)}, {"bytesWritten", double(after.bytesWritten - before.bytesWritten)},
                {"ms", double(levelTimer.elapsed())}});
        }
        const qint64 left = thumbs.pruneDisk();
        QJsonObject policy{{"lodDistance", QJsonArray{config.policy.lodDistance[0], config.policy.lodDistance[1], config.policy.lodDistance[2]}},
            {"requestRatio", QJsonArray{config.policy.requestRatio[0], config.policy.requestRatio[1], config.policy.requestRatio[2]}},
            {"side", QJsonArray{config.policy.side[1], config.policy.side[2], config.policy.side[3]}},
            {"memoryEntries", QJsonArray{config.policy.memoryEntries[1], config.policy.memoryEntries[2], config.policy.memoryEntries[3]}}};
        out.insert("thumbs", QJsonObject{{"directory", config.directory}, {"policy", policy}, {"levels", levels},
            {"diskBytes", double(thumbs.diskBytes())}, {"diskBytesAfterPrune", double(left)}, {"totalMs", double(timer.elapsed())}});
    }
    if (!writeReport(request.outputPath, out)) return 2;
    qInfo().noquote() << QStringLiteral("gallery-layout: %1 photos, %2 piles on %3 row(s), min centre distance %4")
        .arg(scan.photos.size()).arg(layout.piles.size()).arg(layout.rows).arg(layout.minCentreDistance, 0, 'f', 3);
    return result;
}
}
