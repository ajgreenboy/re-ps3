#pragma once
// System Check (plan item P7): a bounded, read-only health report for the HTPC front end.
// Every check finishes in under ~2 s, never prints a secret (tokens, keys, client ids) and never
// writes anything except one temporary file inside the data directory, which it deletes again.
#include <QObject>
#include <QString>
#include <QVector>

namespace xmb {

struct CheckResult {
    enum Level { Ok, Warn, Fail };
    QString id;      // stable id, e.g. "data.writable"
    QString title;   // short row title, e.g. "Data Folder"
    Level level = Ok;
    QString detail;  // what was found (safe to show on screen)
    QString fix;     // what to do about it; empty when there is nothing to do
};

struct SelfCheckInputs {
    QString devFlashRoot;        // Assets::root()
    QString dataDirectory;       // --data-dir or <exe>/data
    QString appVersion;          // QCoreApplication::applicationVersion()
    QString appCommit;           // build-info.json sourceRevision (or empty)
    QString glVersion;           // glGetString(GL_VERSION) from the app's own context
    QString glRenderer;          // glGetString(GL_RENDERER)
    QString glVendor;            // glGetString(GL_VENDOR) (optional)
    QString steamRoot;           // explicit --steam-root; empty = XMB_STEAM_ROOT, registry, defaults
    QString rawgKeyFile;         // empty = RawgClient::apiKeyFilePath() location (AppData/rawg_api_key)
    QString tailscaleExecutable; // empty = PATH, then Program Files
    bool network = true;         // false skips DNS and service probes (offline/scripted runs)
    int timeoutMs = 1500;        // per network/process probe; kept under the 2 s budget
};

QString checkLevelName(CheckResult::Level level);

// Synchronous; call on a worker thread (it blocks on DNS, HTTP and process probes).
QVector<CheckResult> runSelfCheck(const SelfCheckInputs& inputs);

// Runs runSelfCheck on its own thread and emits finished() on this object's thread.
class SelfCheck : public QObject {
    Q_OBJECT
public:
    explicit SelfCheck(QObject* parent = nullptr);
    void setInputs(const SelfCheckInputs& inputs) { inputs_ = inputs; }
    const SelfCheckInputs& inputs() const { return inputs_; }
    bool isRunning() const { return running_; }
public slots:
    void run(); // ignored while a run is in flight
signals:
    void finished(QVector<xmb::CheckResult> results);
private:
    SelfCheckInputs inputs_;
    bool running_ = false;
};

} // namespace xmb
