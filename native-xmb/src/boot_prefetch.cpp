#include "boot_prefetch.h"
#include "boot_profile.h"
#include "host.h"
#include "service_icons.h"
#include "weather_client.h"
#include "ribbon.h"
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <future>
#include <vector>

namespace xmb::boot {
namespace {
QMutex deferredLock;
std::future<void> openGlWarmup;
QOffscreenSurface* openGlSurface=nullptr;
std::vector<std::function<void()>> deferred;
bool deferredStarted=false;

// The first WGL context of a process pays for loading the driver (and the pixel-format search for a 4x multisampled format).
// A throwaway context on a worker thread pays it while the main thread builds the shell; the window's own context then finds the
// driver loaded. Nothing is drawn with it and nothing is shared with it.
void warmOpenGl(QOffscreenSurface* surface) {
    xmb::bootprof::Span span("prefetch OpenGL driver");
    QOpenGLContext context;
    context.setFormat(QSurfaceFormat::defaultFormat());
    if(context.create() && context.makeCurrent(surface)) {
        context.functions()->glGetString(GL_VERSION);
        context.doneCurrent();
    }
}
}

// XMB_BOOT_PREFETCH: unset = every job below; 0 = none (the pre-ZF4 start-up, for an A/B); or a list (comma or plus separated) of the jobs to run:
// ribbon, snapshot, gl, weather, icons.
static bool wanted(const char* job) {
    static const QString setting=qEnvironmentVariable("XMB_BOOT_PREFETCH").replace(QLatin1Char('+'),QLatin1Char(','));
    if(setting.isEmpty())return true;
    if(setting==QLatin1String("0"))return false;
    return setting.split(QLatin1Char(','),Qt::SkipEmptyParts).contains(QLatin1String(job));
}

void startPrefetch(const QString& dataDir) {
    if(wanted("ribbon"))Ribbon::startEarlySettle(QDir(dataDir).filePath(QStringLiteral("cache")));   // the 41,550-step cloth warm-up, started now instead of at View construction
    // The host reading (adapters, printers, audio endpoints, volumes, the token check) the shell's audio wiring asks for first.
    if(wanted("snapshot"))host::startEarlySnapshot();
    // QOffscreenSurface is created on the GUI thread; the context that uses it lives on the worker.
    if(wanted("gl")) {
        openGlSurface=new QOffscreenSurface();
        openGlSurface->setFormat(QSurfaceFormat::defaultFormat());
        openGlSurface->create();
        openGlWarmup=std::async(std::launch::async,[surface=openGlSurface]{warmOpenGl(surface);});
    }
    if(wanted("weather"))QThreadPool::globalInstance()->start([]{
        xmb::bootprof::Span span("prefetch weather icon");
        WeatherClient::icon(WeatherIcon::Partly,false);   // memoised: Shell::initWeather finds it drawn
    });
    if(wanted("icons"))QThreadPool::globalInstance()->start([]{
        xmb::bootprof::Span span("prefetch service icons");
        serviceIcons::tailscale();   // function-local statics: the shell's first call waits for these, never repeats them
        serviceIcons::spotify();
    });
}

void joinOpenGlWarmup() {
    if(openGlWarmup.valid())openGlWarmup.get();
    delete openGlSurface;
    openGlSurface=nullptr;
}

void afterFirstFrame(std::function<void()> job) {
    QMutexLocker lock(&deferredLock);
    if(deferredStarted) {lock.unlock();job();return;}   // the first frame is already out: nothing to wait for
    deferred.push_back(std::move(job));
}

void runDeferred() {
    std::vector<std::function<void()>> jobs;
    {
        QMutexLocker lock(&deferredLock);
        if(deferredStarted)return;
        deferredStarted=true;
        jobs.swap(deferred);
    }
    xmb::bootprof::Span span("deferred jobs");
    for(auto& job:jobs)job();
}
}
