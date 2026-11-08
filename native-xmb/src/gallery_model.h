#pragma once
#include "gallery_params.h"
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

namespace xmb {
// Photo Gallery (hknw_plugin) data model: photo index, date piles and floor layout (audit/PHOTO-GALLERY-DESIGN.md
// step G2). Pure data and file I/O, no GL. The constants that drive the layout come from GalleryParams
// (hknw_plugin/parameters.txt) at run time; the arrangement rules that are reconstructed rather than traced from the
// module are marked INFERRED in gallery_model.cpp and in audit/PHOTO-GALLERY-DESIGN.md.

struct GalleryPhoto {
    QString path;
    qint64 size = 0;
    qint64 modifiedMs = 0;
    // Wall-clock capture time with no zone (EXIF DateTimeOriginal carries none); stored as a UTC-spec QDateTime so
    // the same file always yields the same value on every machine. Without EXIF it is the file time in local time.
    QDateTime taken;
    bool exif = false;
};

// A JPEG's EXIF capture date, read from the first bytes of the file. Qt reads only the EXIF orientation, so this is
// a small bounded APP1/TIFF walk: DateTimeOriginal (0x9003), else DateTimeDigitized (0x9004), else IFD0 DateTime
// (0x0132). head is at most GalleryExifHeadBytes of the file; malformed or truncated data returns false and never
// reads outside it.
constexpr qsizetype GalleryExifHeadBytes = 128 * 1024;
bool parseJpegCaptureDate(const QByteArray& head, QDateTime* taken);
// Reads the head of path (bounded) and parses it. False for non-JPEG data and for files with no valid EXIF date.
bool readJpegCaptureDate(const QString& path, QDateTime* taken);

struct GalleryScanOptions {
    QStringList roots;
    // Hard stops (the module's BOOT_CONTENTS_LIMIT is the upper bound; the practical cap is lower).
    int limit = 20000;
    int maxDepth = 16;
    int maxDirectories = 20000;
    // JSON index of dates already read (path, size, mtime -> date), so a rescan only opens new or changed files.
    // Empty = no cache. Written atomically; unreadable or oversized cache files are ignored.
    QString indexCachePath;
};
struct GalleryScan {
    QVector<GalleryPhoto> photos;  // sorted by capture time, then path
    bool truncated = false;
    bool cancelled = false;
    int directories = 0;
    int exifRead = 0;       // dates taken from EXIF
    int fileTimeUsed = 0;   // dates that fell back to the file time
    int reusedFromIndex = 0;
    QString error;
};
// Blocking: run it on a worker thread. Recursive, name-ordered (so truncation is deterministic), does not follow
// symlinked directories, and stops early when *cancel becomes true.
GalleryScan scanGalleryPhotos(const GalleryScanOptions& options, const std::atomic_bool* cancel = nullptr);

// The same scan on the global thread pool; results arrive on this object's thread.
class GalleryIndexer final : public QObject {
    Q_OBJECT
public:
    explicit GalleryIndexer(QObject* parent = nullptr);
    ~GalleryIndexer() override;
    // Supersedes any scan in flight; returns the request id the finished() signal repeats.
    quint64 start(const GalleryScanOptions& options);
    void cancel();
signals:
    void finished(quint64 requestId, xmb::GalleryScan scan);
private:
    quint64 nextId_ = 0;
    quint64 activeId_ = 0;
    std::shared_ptr<std::atomic_bool> cancel_;
};

enum class GallerySort { Newest, Oldest, Random };
QString gallerySortName(GallerySort sort);
bool parseGallerySort(const QString& text, GallerySort* sort);

struct GalleryLayoutOptions {
    GallerySort sort = GallerySort::Newest;
    quint32 seed = 1;  // Random order only; pile scatter is seeded by the pile id so it never changes
    // Not firmware values (parameters.txt has no field of view): the view the line length is measured against.
    // PARAM_MAX_LINE_LENGTH is "a ratio of the screen width"; the screen width is taken at CAMERA_FAR_LIMIT.
    double fovYDegrees = 45.0;
    double aspect = 16.0 / 9.0;
    // Neighbouring piles keep at least this fraction of their summed half-widths apart (overlap relief).
    double pileClearance = 0.8;
};

struct GalleryPlacedPhoto {
    int photo = -1;      // index into the photo vector
    double x = 0, z = 0; // floor position (world units)
    double rotationY = 0;// degrees
};
struct GalleryPile {
    qint64 id = 0;       // days since 1970-01-01 of the pile's date: stable when other piles come and go
    QDate date;
    QString label;       // yyyy/MM/dd
    int yearBreakBefore = 0; // non-zero: a year label sits between the previous pile and this one
    int colorIndex = 0;  // 0..5, BORDER_LINE_COLOR_n / GROUP_TEXT_COLOR_n
    int row = 0;         // placement line
    double x = 0, z = 0; // pile centre
    double halfWidth = 0, halfDepth = 0;
    QVector<int> photos; // every photo of the pile, in grid order
    int visible = 0;     // piece count drawn on the floor (PARAM_PHOTO_VIEW_COUNT_*)
    QVector<GalleryPlacedPhoto> shown;
};
struct GalleryLayout {
    bool ok = false;
    QString error;
    GallerySort sort = GallerySort::Newest;
    QVector<GalleryPile> piles; // in placement order
    int rows = 0;
    double lineLength = 0;      // world units available per row
    double minCentreDistance = 0; // smallest distance between two pile centres (0 with fewer than two piles)
};
struct GalleryGridCell {
    int photo = -1;
    double x = 0, z = 0;
};

// Names GalleryParams must carry for layoutGallery()/layoutGalleryGrid().
QStringList galleryLayoutParamNames();
GalleryLayout layoutGallery(const QVector<GalleryPhoto>& photos, const GalleryParams& params,
                            const GalleryLayoutOptions& options = {});
// One pile on a grid for the straight-down camera: row-major, cells MIN_DX by MIN_DZ, centred on the origin, about
// the screen's aspect.
QVector<GalleryGridCell> layoutGalleryGrid(const GalleryPile& pile, const GalleryParams& params, double aspect = 16.0 / 9.0);

struct GalleryReportRequest {
    QString devFlash;
    QString dataDir;
    QString outputPath;
    QStringList roots;     // empty = preferences library/photo
    QString sort = QStringLiteral("newest");
    bool warmThumbs = false;
    bool layout = true;    // false = parameters only
    QString thumbDirOverride;
};
// --gallery-params / --gallery-layout: writes the JSON report and returns the process exit code (0 ok).
int runGalleryReport(const GalleryReportRequest& request);
}

Q_DECLARE_METATYPE(xmb::GalleryScan)
