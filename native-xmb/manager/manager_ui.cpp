#include "manager_ui.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace xmb::manager {
namespace {

constexpr int kResultPending = 0;
constexpr int kResultImported = 1;
constexpr int kResultDuplicate = 2;
constexpr int kResultError = 3;

const char* const kCardStyle = "card";
const char* const kMutedStyle = "muted";
const char* const kErrorStyle = "errorLabel";
const char* const kSectionStyle = "sectionTitle";

QString formatBytes(qint64 bytes) {
    if (bytes <= 0)
        return QStringLiteral("-");
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString stateColorName(const QString& state) {
    if (state == QStringLiteral("ok"))
        return QStringLiteral("#8fd18f");
    if (state == QStringLiteral("orphan"))
        return QStringLiteral("#e8bd6a");
    if (state == QStringLiteral("registry-only"))
        return QStringLiteral("#88b4ef");
    if (state == QStringLiteral("invalid"))
        return QStringLiteral("#e58a8a");
    return QStringLiteral("#c7d0dc");
}

QString displayEmulator(const QVector<EmulatorProfile>& profiles, const QString& id) {
    if (id.isEmpty())
        return QStringLiteral("-");
    if (const EmulatorProfile* profile = findProfile(profiles, id))
        return profile->name.isEmpty() ? profile->id : profile->name;
    return id;
}

QString prefPlatformKey(const QString& platform) {
    return QString::fromLatin1(QUrl::toPercentEncoding(platform));
}

QString prefPlatformFromKey(const QString& key) {
    return QUrl::fromPercentEncoding(key.toLatin1());
}

QString environmentText(const QHash<QString, QString>& environment) {
    QStringList keys = environment.keys();
    keys.sort(Qt::CaseInsensitive);
    QStringList lines;
    for (const QString& key : keys)
        lines.append(key + QLatin1Char('=') + environment.value(key));
    return lines.join(QLatin1Char('\n'));
}

bool parseEnvironmentText(const QString& text, QHash<QString, QString>& environment, QString& error) {
    environment.clear();
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        const QString line = lines.at(index).trimmed();
        if (line.isEmpty())
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            error = QStringLiteral("Environment line %1 must look like NAME=value").arg(index + 1);
            return false;
        }
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1);
        if (key.isEmpty()) {
            error = QStringLiteral("Environment line %1 has an empty name").arg(index + 1);
            return false;
        }
        if (environment.contains(key)) {
            error = QStringLiteral("Environment variable '%1' is listed twice").arg(key);
            return false;
        }
        environment.insert(key, value);
    }
    return true;
}

QStringList localPathsFromMime(const QMimeData* mime) {
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = QDir::cleanPath(url.toLocalFile());
        if (!path.isEmpty() && !paths.contains(path, Qt::CaseInsensitive))
            paths.append(path);
    }
    return paths;
}

QString romFilter() {
    QStringList patterns;
    const QStringList extensions = defaultExtensions();
    for (const QString& extension : extensions) {
        if (!extension.isEmpty())
            patterns.append(QStringLiteral("*.") + extension);
    }
    const QString all = patterns.isEmpty() ? QStringLiteral("*") : patterns.join(QLatin1Char(' '));
    return QStringLiteral("ROMs and disc images (%1);;All files (*)").arg(all);
}

QLabel* makeValueLabel(QWidget* parent, const QString& text = QString()) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setObjectName(QStringLiteral("detailValue"));
    return label;
}

QFrame* makeCard(QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setObjectName(QLatin1String(kCardStyle));
    frame->setFrameShape(QFrame::NoFrame);
    return frame;
}

QLabel* makeSectionLabel(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(kSectionStyle));
    return label;
}

QLabel* makeMutedLabel(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(kMutedStyle));
    label->setWordWrap(true);
    return label;
}

QLabel* makeErrorLabel(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName(QLatin1String(kErrorStyle));
    label->setWordWrap(true);
    label->setVisible(false);
    return label;
}

void repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// Library grouping modes (the combo box order).
constexpr int kGroupNone = 0;
constexpr int kGroupCollection = 1;
constexpr int kGroupPlatform = 2;
constexpr int kGroupEmulator = 3;

// Sentinel stored in collectionFilter_ for the "(no collection)" pseudo entry.
QString noCollectionFilter() {
    return QStringLiteral("__xmb_no_collection__");
}

QString displayTags(const QStringList& tags) {
    return tags.isEmpty() ? QStringLiteral("-") : tags.join(QStringLiteral(", "));
}

QString shortHash(const QString& hash) {
    if (hash.size() <= 20)
        return hash;
    return hash.left(12) + QStringLiteral("...") + hash.right(6);
}

void applyMonospace(QPlainTextEdit* edit) {
    if (!edit)
        return;
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(9);
    edit->setFont(font);
}

QString argvText(const QString& executable, const QString& workingDirectory,
                 const QStringList& arguments) {
    QStringList lines;
    lines.append(QStringLiteral("executable: %1").arg(QDir::toNativeSeparators(executable)));
    if (!workingDirectory.trimmed().isEmpty()) {
        lines.append(QStringLiteral("working directory: %1")
                         .arg(QDir::toNativeSeparators(workingDirectory)));
    }
    lines.append(QStringLiteral("argv (%1 element(s), exactly as apps.json stores them):")
                     .arg(arguments.size()));
    for (int position = 0; position < arguments.size(); ++position) {
        lines.append(QStringLiteral("  argv[%1]: %2").arg(position).arg(arguments.at(position)));
    }
    lines.append(QStringLiteral("rendered: %1").arg(formatArguments(arguments)));
    return lines.join(QLatin1Char('\n'));
}

QString mappingSourceText(bool fromMap, bool resolved) {
    if (fromMap)
        return QStringLiteral("map");
    if (resolved)
        return QStringLiteral("profile fallback");
    return QStringLiteral("unmapped");
}

QString mappingSourceColor(const QString& source) {
    if (source == QStringLiteral("map"))
        return QStringLiteral("#8fd18f");
    if (source == QStringLiteral("profile fallback"))
        return QStringLiteral("#88b4ef");
    return QStringLiteral("#e8bd6a");
}

QString outcomeColor(const LaunchOutcome& outcome) {
    if (!outcome.started || !outcome.spawnError.isEmpty())
        return QStringLiteral("#ef9a9a");
    if (outcome.timedOut)
        return QStringLiteral("#e8bd6a");
    if (outcome.exitStatus == QStringLiteral("crash"))
        return QStringLiteral("#e58a8a");
    if (outcome.exitCode != 0)
        return QStringLiteral("#e8bd6a");
    return QStringLiteral("#8fd18f");
}

QString outcomeSummary(const LaunchOutcome& outcome) {
    QStringList lines;
    lines.append(QStringLiteral("executable: %1")
                     .arg(QDir::toNativeSeparators(outcome.executable)));
    if (!outcome.workingDirectory.isEmpty())
        lines.append(QStringLiteral("working directory: %1")
                         .arg(QDir::toNativeSeparators(outcome.workingDirectory)));
    lines.append(QStringLiteral("argv (%1 element(s)):").arg(outcome.arguments.size()));
    for (int index = 0; index < outcome.arguments.size(); ++index) {
        lines.append(QStringLiteral("  argv[%1]: %2")
                         .arg(index)
                         .arg(outcome.arguments.at(index)));
    }
    lines.append(QStringLiteral("rendered: %1").arg(formatArguments(outcome.arguments)));
    if (outcome.pid > 0)
        lines.append(QStringLiteral("pid: %1").arg(outcome.pid));
    if (!outcome.spawnError.isEmpty())
        lines.append(QStringLiteral("spawn error: %1").arg(outcome.spawnError));
    if (outcome.timedOut) {
        lines.append(QStringLiteral("outcome: timed out after %1 s (%2)")
                         .arg(outcome.timeoutSeconds)
                         .arg(outcome.killed
                                  ? QStringLiteral("terminated, then killed")
                                  : (outcome.terminated ? QStringLiteral("terminated")
                                                        : QStringLiteral("still running"))));
    } else if (outcome.finished) {
        lines.append(QStringLiteral("outcome: %1, exit code %2")
                         .arg(outcome.exitStatus)
                         .arg(outcome.exitCode));
    } else {
        lines.append(QStringLiteral("outcome: %1").arg(
            outcome.exitStatus.isEmpty() ? QStringLiteral("unknown") : outcome.exitStatus));
    }
    if (!outcome.stdoutText.trimmed().isEmpty())
        lines.append(QStringLiteral("stdout: %1").arg(outcome.stdoutText.trimmed()));
    if (!outcome.stderrText.trimmed().isEmpty())
        lines.append(QStringLiteral("stderr: %1").arg(outcome.stderrText.trimmed()));
    return lines.join(QLatin1Char('\n'));
}

class DropZone : public QFrame {
public:
    explicit DropZone(QWidget* parent) : QFrame(parent) {
        setObjectName(QStringLiteral("importDropZone"));
        setAcceptDrops(true);
        setMinimumHeight(104);
        setFrameShape(QFrame::NoFrame);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 14, 20, 14);
        layout->setSpacing(4);
        auto* title = new QLabel(QStringLiteral("Drop ROM, ISO, and folder here"), this);
        title->setObjectName(QStringLiteral("dropZoneTitle"));
        auto* hint = new QLabel(
            QStringLiteral("Files and folders are scanned only; nothing is written until you press "
                           "Import selected."),
            this);
        hint->setObjectName(QStringLiteral("dropZoneHint"));
        hint->setWordWrap(true);
        layout->addWidget(title);
        layout->addWidget(hint);
    }

    std::function<void(const QStringList&)> onPathsDropped;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (localPathsFromMime(event->mimeData()).isEmpty()) {
            event->ignore();
            return;
        }
        setDragActive(true);
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        setDragActive(false);
        QFrame::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent* event) override {
        setDragActive(false);
        const QStringList paths = localPathsFromMime(event->mimeData());
        if (paths.isEmpty()) {
            event->ignore();
            return;
        }
        if (onPathsDropped)
            onPathsDropped(paths);
        event->acceptProposedAction();
    }

private:
    void setDragActive(bool active) {
        setProperty("dragActive", active);
        repolish(this);
    }
};

} // namespace

MainWindow::MainWindow(Settings settings, QWidget* parent)
    : QMainWindow(parent), settings_(std::move(settings)) {
    setObjectName(QStringLiteral("xmbManagerWindow"));
    setWindowTitle(QStringLiteral("XMB Manager"));
    setMinimumSize(1080, 680);
    resize(1320, 860);
    setAcceptDrops(true);

    const Settings defaults = defaultSettings();
    if (settings_.executableDirectory.trimmed().isEmpty())
        settings_.executableDirectory = defaults.executableDirectory;
    if (settings_.iniPath.trimmed().isEmpty())
        settings_.iniPath = defaults.iniPath;
    if (settings_.dataDirectory.trimmed().isEmpty()) {
        QString loadError;
        if (!loadSettings(settings_, loadError))
            startupErrors_.append(QStringLiteral("Settings: %1").arg(loadError));
    }
    if (settings_.dataDirectory.trimmed().isEmpty())
        settings_.dataDirectory = defaults.dataDirectory;
    if (settings_.iniPath.trimmed().isEmpty())
        settings_.iniPath = defaultSettingsPath();

    QDir().mkpath(settings_.executableDirectory);
    uiSettings_ = new QSettings(QDir(settings_.executableDirectory).filePath(
                                    QStringLiteral("manager-ui.ini")),
                                QSettings::IniFormat);

    applyTheme();
    buildUi();
    restoreUiState();
    loadUiPreferences();
    connectShortcuts();
    ensureLayoutAndRefresh();
}

MainWindow::~MainWindow() {
    importCancel_.store(true);
    waitForTasks();
    detachRunningProcesses();
    delete uiSettings_;
    uiSettings_ = nullptr;
}

void MainWindow::applyTheme() {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, QColor(20, 23, 29));
    palette.setColor(QPalette::WindowText, QColor(237, 240, 245));
    palette.setColor(QPalette::Base, QColor(18, 21, 27));
    palette.setColor(QPalette::AlternateBase, QColor(24, 28, 35));
    palette.setColor(QPalette::Text, QColor(237, 240, 245));
    palette.setColor(QPalette::Button, QColor(36, 43, 54));
    palette.setColor(QPalette::ButtonText, QColor(232, 237, 245));
    palette.setColor(QPalette::Highlight, QColor(47, 95, 147));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipBase, QColor(32, 38, 48));
    palette.setColor(QPalette::ToolTipText, QColor(237, 240, 245));
    palette.setColor(QPalette::PlaceholderText, QColor(120, 132, 148));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(105, 115, 128));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(105, 115, 128));
    qApp->setPalette(palette);
    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSize(10);
    qApp->setFont(font);

    qApp->setStyleSheet(QStringLiteral(R"QSS(
QMainWindow { background: #14171d; }
QWidget { color: #edf0f5; font-family: "Segoe UI"; font-size: 10pt; }
QWidget#centralRoot { background: #14171d; }
QFrame#headerCard, QFrame#card {
    background: #1b1f27; border: 1px solid #2b323d; border-radius: 8px;
}
QFrame#infoBanner {
    background: #1a2633; border: 1px solid #33506b; border-radius: 6px;
}
QLabel#appTitle { font-size: 22pt; font-weight: 600; color: #f5f8fc; }
QLabel#appSubtitle { color: #8e99a8; }
QLabel#sectionTitle { font-size: 12pt; font-weight: 600; color: #eef3f9; }
QLabel#detailTitle { font-size: 14pt; font-weight: 600; color: #f5f8fc; }
QLabel#muted, QLabel#detailCaption, QLabel#dropZoneHint, QLabel#hint {
    color: #8e99a8;
}
QLabel#detailValue { color: #dfe6f0; }
QLabel#errorLabel { color: #ef9a9a; }
QLabel#detectorStatus { color: #a9bed6; }
QLabel#dropZoneTitle { font-size: 12pt; font-weight: 600; color: #dfe9f5; }
QLabel#artworkPreview {
    background: #12151b; border: 1px solid #2b323d; border-radius: 6px;
    color: #6f7b8b;
}
QLineEdit, QComboBox, QPlainTextEdit, QTableWidget, QTreeWidget {
    background: #12151b; color: #edf0f5; border: 1px solid #2f3844;
    border-radius: 4px; padding: 5px;
    selection-background-color: #2f5f93; selection-color: #ffffff;
}
QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus {
    border: 1px solid #4a7fb5;
}
QComboBox QAbstractItemView {
    background: #1b1f27; color: #edf0f5; border: 1px solid #2f3844;
    selection-background-color: #2f5f93;
}
QPushButton {
    background: #242b36; color: #e8edf5; border: 1px solid #39434f;
    border-radius: 4px; padding: 6px 12px;
}
QPushButton:hover { background: #2e3845; border-color: #4a5868; }
QPushButton:pressed { background: #1d242d; }
QPushButton:disabled { color: #66707c; border-color: #2a313a; background: #1b2027; }
QPushButton#accentButton {
    background: #2f5f93; border-color: #447eb8; font-weight: 600; color: #ffffff;
}
QPushButton#accentButton:hover { background: #386fa8; }
QPushButton#dangerButton { color: #efb3b3; }
QPushButton#dangerButton:hover { background: #3a2628; border-color: #6a3a3e; }
QTabWidget::pane {
    border: 1px solid #2b323d; border-radius: 6px; background: #181c23;
    top: -1px;
}
QTabBar::tab {
    background: transparent; color: #9aa5b4; padding: 9px 18px;
    border-bottom: 2px solid transparent;
}
QTabBar::tab:selected { color: #f2f5fa; border-bottom: 2px solid #4f8ac9; }
QTabBar::tab:hover { color: #dfe7f2; }
QHeaderView::section {
    background: #202630; color: #aab5c4; border: none;
    border-right: 1px solid #2b323d; border-bottom: 1px solid #2b323d;
    padding: 6px;
}
QTableWidget { gridline-color: #262d37; alternate-background-color: #161b22; }
QTableWidget::item { padding: 4px; }
QTableWidget::item:selected { background: #2b517a; color: #ffffff; }
QTreeWidget::item:selected { background: #2b517a; color: #ffffff; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical {
    background: #14171d; width: 12px; margin: 0px;
}
QScrollBar::handle:vertical {
    background: #39434f; min-height: 24px; border-radius: 5px; margin: 2px;
}
QScrollBar::handle:vertical:hover { background: #4a5868; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar:horizontal {
    background: #14171d; height: 12px; margin: 0px;
}
QScrollBar::handle:horizontal {
    background: #39434f; min-width: 24px; border-radius: 5px; margin: 2px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
QProgressBar {
    border: 1px solid #2f3844; background: #12151b; border-radius: 4px;
    text-align: center; color: #dfe6f0; height: 16px;
}
QProgressBar::chunk { background: #3f78b0; border-radius: 3px; }
QStatusBar { background: #101318; color: #8e99a8; }
QStatusBar QLabel { color: #a9bed6; }
QFrame#importDropZone {
    border: 2px dashed #3c4a5c; border-radius: 8px; background: #171c24;
}
QFrame#importDropZone[dragActive="true"] {
    border: 2px dashed #5f9ad6; background: #1b2532;
}
QSplitter::handle { background: #202630; }
QSplitter::handle:hover { background: #2f3844; }
QToolTip {
    background: #202630; color: #edf0f5; border: 1px solid #3c4a5c; padding: 6px;
}
QPushButton#testLaunchButton, QPushButton#mappingSaveButton {
    background: #2f5f93; border-color: #447eb8; font-weight: 600; color: #ffffff;
}
QPushButton#testLaunchButton:hover, QPushButton#mappingSaveButton:hover { background: #386fa8; }
QPushButton#testLaunchButton:disabled, QPushButton#mappingSaveButton:disabled {
    background: #1b2027; color: #66707c;
}
QDialog { background: #1b1f27; }
QMessageBox { background: #1b1f27; }
QMessageBox QLabel { color: #edf0f5; }
QMenu { background: #1b1f27; color: #edf0f5; border: 1px solid #2f3844; }
QMenu::item:selected { background: #2f5f93; }
QMenuBar { background: #101318; color: #c7d0dc; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected { background: #242b36; color: #ffffff; }
QListWidget {
    background: #12151b; color: #edf0f5; border: 1px solid #2f3844;
    border-radius: 4px; padding: 3px;
}
QListWidget::item { padding: 5px 6px; border-radius: 3px; }
QListWidget::item:selected { background: #2b517a; color: #ffffff; }
QFrame#bulkBar {
    background: #1a2633; border: 1px solid #33506b; border-radius: 6px;
}
QLabel#bulkLabel { color: #cfe0f2; font-weight: 600; }
QFrame#warningBanner {
    background: #332a1a; border: 1px solid #6b552f; border-radius: 6px;
}
QLabel#warningText { color: #e8bd6a; }
QFrame#mapBanner {
    background: #1a2633; border: 1px solid #33506b; border-radius: 6px;
}
QPushButton#tagChip {
    background: #202630; color: #aab5c4; border: 1px solid #2f3844;
    border-radius: 10px; padding: 2px 10px; font-size: 9pt;
}
QPushButton#tagChip:hover { background: #2b3542; color: #dfe7f2; }
QPushButton#tagChip:checked {
    background: #2f5f93; color: #ffffff; border: 1px solid #447eb8;
}
QPlainTextEdit#argvView, QLabel#hashValue {
    font-family: "Consolas", "Courier New", monospace;
    color: #cfe0f2;
}
QCheckBox { color: #dfe6f0; }
QSpinBox {
    background: #12151b; color: #edf0f5; border: 1px solid #2f3844;
    border-radius: 4px; padding: 4px;
}
QLabel#badge {
    background: #202630; color: #aab5c4; border: 1px solid #2f3844;
    border-radius: 9px; padding: 2px 8px; font-size: 9pt;
}
QLabel#rowMeta { color: #8e99a8; }
)QSS"));
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralRoot"));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 14, 16, 10);
    root->setSpacing(10);
    root->addWidget(buildHeader());
    tabs_ = new QTabWidget(central);
    tabs_->setObjectName(QStringLiteral("tabWidget"));
    tabs_->setDocumentMode(true);
    libraryTab_ = buildLibraryTab();
    importTab_ = buildImportTab();
    emulatorsTab_ = buildEmulatorsTab();
    platformMapTab_ = buildPlatformMapTab();
    diagnosticsTab_ = buildDiagnosticsTab();
    settingsTab_ = buildSettingsTab();
    tabs_->addTab(libraryTab_, QStringLiteral("Library"));
    tabs_->addTab(importTab_, QStringLiteral("Import"));
    tabs_->addTab(emulatorsTab_, QStringLiteral("Emulators"));
    tabs_->addTab(platformMapTab_, QStringLiteral("Platform map"));
    tabs_->addTab(diagnosticsTab_, QStringLiteral("Diagnostics"));
    tabs_->addTab(settingsTab_, QStringLiteral("Settings"));
    root->addWidget(tabs_, 1);
    setCentralWidget(central);
    buildStatusBar();
    buildMenus();
}

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* addFilesAction = fileMenu->addAction(QStringLiteral("Add ROM files..."));
    addFilesAction->setShortcut(QKeySequence::Open);
    connect(addFilesAction, &QAction::triggered, this, &MainWindow::addFiles);
    QAction* addFolderAction = fileMenu->addAction(QStringLiteral("Add folder..."));
    addFolderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(addFolderAction, &QAction::triggered, this, &MainWindow::addFolder);
    fileMenu->addSeparator();
    backupAction_ = fileMenu->addAction(QStringLiteral("Backup library..."));
    backupAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    backupAction_->setToolTip(
        QStringLiteral("Write a portable xmb-manager-backup/1 bundle (Ctrl+B)"));
    connect(backupAction_, &QAction::triggered, this, &MainWindow::backupLibrary);
    restoreAction_ = fileMenu->addAction(QStringLiteral("Restore library..."));
    restoreAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    restoreAction_->setToolTip(
        QStringLiteral("Restore a backup bundle, with optional path remaps (Ctrl+Shift+B)"));
    connect(restoreAction_, &QAction::triggered, this, &MainWindow::restoreLibrary);
    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(QStringLiteral("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);
    // The same file shortcuts also work when a menu bar is not visible.
    addAction(backupAction_);
    addAction(restoreAction_);

    QMenu* libraryMenu = menuBar()->addMenu(QStringLiteral("&Library"));
    testLaunchAction_ = libraryMenu->addAction(QStringLiteral("Test launch"));
    testLaunchAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    testLaunchAction_->setToolTip(
        QStringLiteral("Call the core's launchPackage for the selected package (Ctrl+T)"));
    connect(testLaunchAction_, &QAction::triggered, this, &MainWindow::launchLibraryEntry);
    showArgvAction_ = libraryMenu->addAction(QStringLiteral("Show argv"));
    showArgvAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    showArgvAction_->setToolTip(
        QStringLiteral("Print the exact apps.json argv for the selected package (Ctrl+K)"));
    connect(showArgvAction_, &QAction::triggered, this, &MainWindow::showArgvForSelected);
    libraryMenu->addSeparator();
    QAction* editAction = libraryMenu->addAction(QStringLiteral("Edit package..."));
    editAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(editAction, &QAction::triggered, this, &MainWindow::editLibraryEntry);
    editTagsAction_ = libraryMenu->addAction(QStringLiteral("Edit tags..."));
    editTagsAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    connect(editTagsAction_, &QAction::triggered, this, &MainWindow::editLibraryTags);
    QAction* collectionAction = libraryMenu->addAction(QStringLiteral("Add to collection..."));
    connect(collectionAction, &QAction::triggered, this, &MainWindow::assignSelectedToCollection);
    libraryMenu->addSeparator();
    QAction* relinkAction = libraryMenu->addAction(QStringLiteral("Relink ROM..."));
    connect(relinkAction, &QAction::triggered, this, &MainWindow::relinkSelectedPackage);
    QAction* removeAction = libraryMenu->addAction(QStringLiteral("Remove package..."));
    connect(removeAction, &QAction::triggered, this, &MainWindow::removeLibraryEntry);
    addAction(removeAction);

    QMenu* diagnosticsMenu = menuBar()->addMenu(QStringLiteral("&Diagnostics"));
    verifyHashesAction_ = diagnosticsMenu->addAction(QStringLiteral("Verify hashes"));
    verifyHashesAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    verifyHashesAction_->setToolTip(
        QStringLiteral("Run verifyPackageHashes over the whole library (Ctrl+Shift+H)"));
    connect(verifyHashesAction_, &QAction::triggered, this, &MainWindow::verifyLibraryHashes);
    QAction* copyReportAction = diagnosticsMenu->addAction(QStringLiteral("Copy report"));
    connect(copyReportAction, &QAction::triggered, this, &MainWindow::copyDiagnostics);
    QAction* reportPathAction = diagnosticsMenu->addAction(QStringLiteral("Copy report path"));
    connect(reportPathAction, &QAction::triggered, this, &MainWindow::copyReportPath);
    QAction* openFolderAction = diagnosticsMenu->addAction(QStringLiteral("Open data folder"));
    connect(openFolderAction, &QAction::triggered, this, &MainWindow::openDataFolder);
}

QWidget* MainWindow::buildHeader() {
    auto* card = makeCard(this);
    card->setObjectName(QStringLiteral("headerCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* titleRow = new QHBoxLayout;
    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("XMB Manager"), card);
    title->setObjectName(QStringLiteral("appTitle"));
    auto* subtitle = new QLabel(
        QStringLiteral("Companion tooling for the native PS3 XMB: emulators, virtual packages, "
                       "and library upkeep."),
        card);
    subtitle->setObjectName(QStringLiteral("appSubtitle"));
    subtitle->setWordWrap(true);
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    titleRow->addLayout(titleBlock, 1);
    refreshButton_ = new QPushButton(QStringLiteral("Refresh"), card);
    refreshButton_->setToolTip(QStringLiteral("Reload packages and emulator profiles (Ctrl+R)"));
    refreshButton_->setAccessibleName(QStringLiteral("Refresh library and emulators"));
    titleRow->addWidget(refreshButton_, 0, Qt::AlignTop);
    layout->addLayout(titleRow);

    auto* dataRow = new QHBoxLayout;
    dataRow->setSpacing(8);
    auto* dataCaption = new QLabel(QStringLiteral("Data folder"), card);
    dataCaption->setObjectName(QStringLiteral("muted"));
    dataRow->addWidget(dataCaption);
    dataDirectoryEdit_ = new QLineEdit(card);
    dataDirectoryEdit_->setObjectName(QStringLiteral("dataDirectoryEdit"));
    dataDirectoryEdit_->setReadOnly(true);
    dataDirectoryEdit_->setToolTip(
        QStringLiteral("The manager data folder. Packages live in <data>/pkg/<id>/."));
    dataDirectoryEdit_->setAccessibleName(QStringLiteral("Manager data folder"));
    dataRow->addWidget(dataDirectoryEdit_, 1);
    browseDataButton_ = new QPushButton(QStringLiteral("Browse..."), card);
    browseDataButton_->setToolTip(QStringLiteral("Choose a different manager data folder"));
    dataRow->addWidget(browseDataButton_);
    openDataButton_ = new QPushButton(QStringLiteral("Open data folder"), card);
    openDataButton_->setToolTip(QStringLiteral("Open the data folder in File Explorer"));
    dataRow->addWidget(openDataButton_);
    layout->addLayout(dataRow);

    detectorLabel_ = new QLabel(QStringLiteral("Checking for installed emulators..."), card);
    detectorLabel_->setObjectName(QStringLiteral("detectorStatus"));
    detectorLabel_->setWordWrap(true);
    detectorLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(detectorLabel_);

    xmbBanner_ = new QFrame(card);
    xmbBanner_->setObjectName(QStringLiteral("infoBanner"));
    auto* bannerLayout = new QHBoxLayout(xmbBanner_);
    bannerLayout->setContentsMargins(10, 7, 10, 7);
    bannerLayout->setSpacing(8);
    xmbBannerLabel_ = new QLabel(xmbBanner_);
    xmbBannerLabel_->setWordWrap(true);
    xmbBannerLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bannerLayout->addWidget(xmbBannerLabel_, 1);
    useXmbButton_ = new QPushButton(QStringLiteral("Use this folder"), xmbBanner_);
    useXmbButton_->setToolTip(
        QStringLiteral("Switch the manager to the data folder the XMB launches from"));
    bannerLayout->addWidget(useXmbButton_);
    xmbBanner_->setVisible(false);
    layout->addWidget(xmbBanner_);

    headerNotice_ = makeErrorLabel(card);
    layout->addWidget(headerNotice_);

    connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshAll(true); });
    connect(browseDataButton_, &QPushButton::clicked, this, &MainWindow::browseDataDirectory);
    connect(openDataButton_, &QPushButton::clicked, this, &MainWindow::openDataFolder);
    connect(useXmbButton_, &QPushButton::clicked, this,
            &MainWindow::useDetectedXmbDataDirectory);

    return card;
}

QWidget* MainWindow::buildLibraryTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(8);
    topRow->addWidget(makeSectionLabel(page, QStringLiteral("Packages")));
    libraryCountLabel_ = makeMutedLabel(page, QStringLiteral("0 packages"));
    libraryCountLabel_->setWordWrap(false);
    topRow->addWidget(libraryCountLabel_);
    topRow->addStretch(1);
    auto* groupCaption = new QLabel(QStringLiteral("Group by"), page);
    groupCaption->setObjectName(QStringLiteral("muted"));
    topRow->addWidget(groupCaption);
    groupCombo_ = new QComboBox(page);
    groupCombo_->setObjectName(QStringLiteral("libraryGroupCombo"));
    groupCombo_->addItem(QStringLiteral("None"));
    groupCombo_->addItem(QStringLiteral("Collection"));
    groupCombo_->addItem(QStringLiteral("Platform"));
    groupCombo_->addItem(QStringLiteral("Emulator"));
    groupCombo_->setToolTip(
        QStringLiteral("Insert group headers with counts above the matching packages"));
    topRow->addWidget(groupCombo_);
    librarySearch_ = new QLineEdit(page);
    librarySearch_->setObjectName(QStringLiteral("librarySearch"));
    librarySearch_->setPlaceholderText(
        QStringLiteral("Search title, platform, collection, or tag..."));
    librarySearch_->setClearButtonEnabled(true);
    librarySearch_->setMaximumWidth(280);
    librarySearch_->setToolTip(QStringLiteral("Filter the library table (Ctrl+F)"));
    librarySearch_->setAccessibleName(QStringLiteral("Search packages"));
    topRow->addWidget(librarySearch_);
    auto* clearFiltersButton = new QPushButton(QStringLiteral("Clear filters"), page);
    clearFiltersButton->setToolTip(QStringLiteral("Clear the search, tag, and collection filters"));
    topRow->addWidget(clearFiltersButton);
    layout->addLayout(topRow);

    auto* tagRow = new QHBoxLayout;
    tagRow->setSpacing(6);
    auto* tagCaption = new QLabel(QStringLiteral("Tags"), page);
    tagCaption->setObjectName(QStringLiteral("muted"));
    tagRow->addWidget(tagCaption);
    auto* tagScroll = new QScrollArea(page);
    tagScroll->setObjectName(QStringLiteral("tagFilterScroll"));
    tagScroll->setFrameShape(QFrame::NoFrame);
    tagScroll->setWidgetResizable(true);
    tagScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tagScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tagScroll->setFixedHeight(38);
    tagChipsHost_ = new QWidget(tagScroll);
    tagChipsHost_->setObjectName(QStringLiteral("tagChipsHost"));
    tagChipsLayout_ = new QHBoxLayout(tagChipsHost_);
    tagChipsLayout_->setContentsMargins(0, 0, 0, 0);
    tagChipsLayout_->setSpacing(6);
    tagChipsLayout_->addStretch(1);
    tagScroll->setWidget(tagChipsHost_);
    tagRow->addWidget(tagScroll, 1);
    tagFilterSummary_ = makeMutedLabel(page, QStringLiteral("No tags yet"));
    tagFilterSummary_->setWordWrap(false);
    tagRow->addWidget(tagFilterSummary_);
    layout->addLayout(tagRow);

    bulkBar_ = new QFrame(page);
    bulkBar_->setObjectName(QStringLiteral("bulkBar"));
    auto* bulkLayout = new QHBoxLayout(bulkBar_);
    bulkLayout->setContentsMargins(10, 6, 10, 6);
    bulkLayout->setSpacing(6);
    bulkLabel_ = new QLabel(QStringLiteral("0 selected"), bulkBar_);
    bulkLabel_->setObjectName(QStringLiteral("bulkLabel"));
    bulkLayout->addWidget(bulkLabel_);
    bulkLayout->addSpacing(6);
    bulkCollectionButton_ = new QPushButton(QStringLiteral("Add to collection..."), bulkBar_);
    bulkCollectionButton_->setObjectName(QStringLiteral("bulkCollectionButton"));
    bulkCollectionButton_->setToolTip(
        QStringLiteral("Move every selected package into one collection through applyBulk"));
    bulkEmulatorButton_ = new QPushButton(QStringLiteral("Set emulator..."), bulkBar_);
    bulkEmulatorButton_->setObjectName(QStringLiteral("bulkEmulatorButton"));
    bulkEmulatorButton_->setToolTip(
        QStringLiteral("Re-expand and register the selected packages against one profile"));
    bulkAddTagButton_ = new QPushButton(QStringLiteral("Add tags..."), bulkBar_);
    bulkAddTagButton_->setObjectName(QStringLiteral("bulkAddTagButton"));
    bulkRemoveTagButton_ = new QPushButton(QStringLiteral("Remove tags..."), bulkBar_);
    bulkRemoveTagButton_->setObjectName(QStringLiteral("bulkRemoveTagButton"));
    bulkRemoveButton_ = new QPushButton(QStringLiteral("Remove packages..."), bulkBar_);
    bulkRemoveButton_->setObjectName(QStringLiteral("bulkRemoveButton"));
    bulkRemoveButton_->setStyleSheet(QStringLiteral("QPushButton { color: #efb3b3; }"));
    bulkRemoveButton_->setToolTip(
        QStringLiteral("Delete the packages and their registrations; ROMs are never touched"));
    bulkClearButton_ = new QPushButton(QStringLiteral("Clear selection"), bulkBar_);
    bulkClearButton_->setObjectName(QStringLiteral("bulkClearButton"));
    bulkLayout->addWidget(bulkCollectionButton_);
    bulkLayout->addWidget(bulkEmulatorButton_);
    bulkLayout->addWidget(bulkAddTagButton_);
    bulkLayout->addWidget(bulkRemoveTagButton_);
    bulkLayout->addWidget(bulkRemoveButton_);
    bulkLayout->addStretch(1);
    bulkLayout->addWidget(bulkClearButton_);
    bulkBar_->setVisible(false);
    layout->addWidget(bulkBar_);

    libraryMessage_ = makeErrorLabel(page);
    layout->addWidget(libraryMessage_);

    libraryStack_ = new QStackedWidget(page);
    libraryStack_->setObjectName(QStringLiteral("libraryStack"));

    libraryEmptyPage_ = new QWidget(libraryStack_);
    auto* emptyLayout = new QVBoxLayout(libraryEmptyPage_);
    emptyLayout->setContentsMargins(24, 24, 24, 24);
    emptyLayout->addStretch(1);
    auto* emptyCard = makeCard(libraryEmptyPage_);
    emptyCard->setFixedWidth(580);
    auto* emptyCardLayout = new QVBoxLayout(emptyCard);
    emptyCardLayout->setContentsMargins(28, 24, 28, 24);
    emptyCardLayout->setSpacing(10);
    auto* emptyTitle = new QLabel(QStringLiteral("No packages yet"), emptyCard);
    emptyTitle->setObjectName(QStringLiteral("detailTitle"));
    emptyTitle->setAlignment(Qt::AlignHCenter);
    auto* emptyText = makeMutedLabel(
        emptyCard,
        QStringLiteral("The XMB library is empty. Drag ROMs, ISOs, or a folder onto the Import "
                       "tab, then press Import selected to create virtual PS3-style packages."));
    emptyText->setAlignment(Qt::AlignHCenter);
    emptyText->setMinimumHeight(58);
    emptyCardLayout->addWidget(emptyTitle);
    emptyCardLayout->addWidget(emptyText);
    auto* emptyButtons = new QHBoxLayout;
    emptyButtons->addStretch(1);
    auto* goImportButton = new QPushButton(QStringLiteral("Go to Import"), emptyCard);
    goImportButton->setObjectName(QStringLiteral("accentButton"));
    goImportButton->setToolTip(QStringLiteral("Open the Import tab"));
    auto* addNowButton = new QPushButton(QStringLiteral("Add files..."), emptyCard);
    addNowButton->setToolTip(QStringLiteral("Choose ROM files to scan (Ctrl+O)"));
    emptyButtons->addWidget(goImportButton);
    emptyButtons->addWidget(addNowButton);
    emptyButtons->addStretch(1);
    emptyCardLayout->addLayout(emptyButtons);
    emptyLayout->addWidget(emptyCard, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);
    libraryStack_->addWidget(libraryEmptyPage_);

    libraryTablePage_ = new QWidget(libraryStack_);
    auto* tablePageLayout = new QVBoxLayout(libraryTablePage_);
    tablePageLayout->setContentsMargins(0, 0, 0, 0);
    librarySplitter_ = new QSplitter(Qt::Horizontal, libraryTablePage_);
    librarySplitter_->setChildrenCollapsible(false);

    auto* collectionsCard = makeCard(librarySplitter_);
    collectionsCard->setObjectName(QStringLiteral("card"));
    collectionsCard->setMinimumWidth(214);
    collectionsCard->setMaximumWidth(320);
    auto* collectionsLayout = new QVBoxLayout(collectionsCard);
    collectionsLayout->setContentsMargins(12, 10, 12, 10);
    collectionsLayout->setSpacing(6);
    collectionsLayout->addWidget(
        makeSectionLabel(collectionsCard, QStringLiteral("Collections")));
    collectionsMessage_ = makeErrorLabel(collectionsCard);
    collectionsLayout->addWidget(collectionsMessage_);
    collectionsList_ = new QListWidget(collectionsCard);
    collectionsList_->setObjectName(QStringLiteral("collectionsList"));
    collectionsList_->setToolTip(
        QStringLiteral("Selecting a collection filters the table. Counts come from "
                       "collections.json."));
    collectionsList_->setAccessibleName(QStringLiteral("Collections"));
    collectionsLayout->addWidget(collectionsList_, 1);
    collectionAddButton_ = new QPushButton(QStringLiteral("New..."), collectionsCard);
    collectionAddButton_->setObjectName(QStringLiteral("collectionNewButton"));
    collectionAddButton_->setToolTip(QStringLiteral("Create a collection and assign the selection"));
    collectionRenameButton_ = new QPushButton(QStringLiteral("Rename..."), collectionsCard);
    collectionRenameButton_->setObjectName(QStringLiteral("collectionRenameButton"));
    collectionRenameButton_->setToolTip(QStringLiteral("Rename through the core's renameCollection"));
    collectionRemoveButton_ = new QPushButton(QStringLiteral("Remove"), collectionsCard);
    collectionRemoveButton_->setObjectName(QStringLiteral("collectionRemoveButton"));
    collectionRemoveButton_->setStyleSheet(QStringLiteral("QPushButton { color: #efb3b3; }"));
    collectionRemoveButton_->setToolTip(
        QStringLiteral("Remove through the core's removeCollection; packages are not deleted"));
    auto* collectionButtons = new QGridLayout;
    collectionButtons->setHorizontalSpacing(4);
    collectionButtons->setVerticalSpacing(4);
    collectionButtons->addWidget(collectionAddButton_, 0, 0, 1, 2);
    collectionButtons->addWidget(collectionRenameButton_, 1, 0);
    collectionButtons->addWidget(collectionRemoveButton_, 1, 1);
    collectionsLayout->addLayout(collectionButtons);
    collectionsLayout->addWidget(makeMutedLabel(
        collectionsCard,
        QStringLiteral("A package belongs to one collection. Emptying the name clears it.")));

    auto* tableHost = new QWidget(librarySplitter_);
    auto* tableLayout = new QVBoxLayout(tableHost);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(4);
    libraryTable_ = new QTableWidget(0, 7, tableHost);
    libraryTable_->setObjectName(QStringLiteral("libraryTable"));
    libraryTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Title"), QStringLiteral("Platform"), QStringLiteral("Emulator"),
         QStringLiteral("Collection"), QStringLiteral("Tags"), QStringLiteral("Size"),
         QStringLiteral("State")});
    libraryTable_->verticalHeader()->setVisible(false);
    libraryTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    libraryTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    libraryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    libraryTable_->setAlternatingRowColors(true);
    libraryTable_->setShowGrid(false);
    libraryTable_->setSortingEnabled(false);
    libraryTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    libraryTable_->horizontalHeader()->setHighlightSections(false);
    libraryTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    libraryTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    for (int column : {1, 2, 3, 5, 6})
        libraryTable_->horizontalHeader()->setSectionResizeMode(column,
                                                                QHeaderView::ResizeToContents);
    libraryTable_->setToolTip(
        QStringLiteral("Packages registered in apps.json and stored under pkg/. Ctrl-click or "
                       "Shift-click to select several packages."));
    libraryTable_->setAccessibleName(QStringLiteral("Installed packages"));
    tableLayout->addWidget(libraryTable_, 1);
    auto* tableHint = makeMutedLabel(
        tableHost,
        QStringLiteral("Ctrl+R refreshes. Ctrl-click or Shift-click selects several packages for "
                       "the bulk bar. Delete removes the selection."));
    tableLayout->addWidget(tableHint);

    auto* detailScroll = new QScrollArea(librarySplitter_);
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    auto* detailCard = makeCard(detailScroll);
    auto* detailLayout = new QVBoxLayout(detailCard);
    detailLayout->setContentsMargins(14, 12, 14, 12);
    detailLayout->setSpacing(7);
    artworkLabel_ = new QLabel(detailCard);
    artworkLabel_->setObjectName(QStringLiteral("artworkPreview"));
    artworkLabel_->setAlignment(Qt::AlignCenter);
    artworkLabel_->setMinimumHeight(130);
    artworkLabel_->setMaximumHeight(160);
    artworkLabel_->setText(QStringLiteral("No artwork"));
    detailLayout->addWidget(artworkLabel_);

    detailTitle_ = new QLabel(detailCard);
    detailTitle_->setObjectName(QStringLiteral("detailTitle"));
    detailTitle_->setWordWrap(true);
    detailTitle_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(detailTitle_);
    detailState_ = new QLabel(detailCard);
    detailState_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailState_->setWordWrap(true);
    detailLayout->addWidget(detailState_);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);
    int gridRow = 0;
    const auto addRow = [&](const QString& caption, QLabel*& value) {
        auto* captionLabel = new QLabel(caption, detailCard);
        captionLabel->setObjectName(QStringLiteral("detailCaption"));
        captionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        value = makeValueLabel(detailCard);
        grid->addWidget(captionLabel, gridRow, 0);
        grid->addWidget(value, gridRow, 1);
        ++gridRow;
    };
    addRow(QStringLiteral("Platform"), detailPlatform_);
    addRow(QStringLiteral("Emulator"), detailEmulator_);
    addRow(QStringLiteral("ROM"), detailRom_);
    addRow(QStringLiteral("Collection"), detailCollection_);
    addRow(QStringLiteral("Tags"), detailTags_);
    addRow(QStringLiteral("Size"), detailSize_);
    addRow(QStringLiteral("Arguments"), detailArguments_);
    addRow(QStringLiteral("Package"), detailPackage_);
    detailLayout->addLayout(grid);
    auto* hashCaption = new QLabel(QStringLiteral("Hash (label | value)"), detailCard);
    hashCaption->setObjectName(QStringLiteral("detailCaption"));
    detailLayout->addWidget(hashCaption);
    detailHash_ = makeValueLabel(detailCard);
    detailHash_->setObjectName(QStringLiteral("hashValue"));
    detailHash_->setWordWrap(true);
    detailLayout->addWidget(detailHash_);
    detailHashState_ = makeMutedLabel(detailCard, QStringLiteral("Hash not verified this session"));
    detailHashState_->setObjectName(QStringLiteral("detailHashState"));
    detailLayout->addWidget(detailHashState_);

    auto* argvCaption = new QLabel(QStringLiteral("Exact argv (monospace)"), detailCard);
    argvCaption->setObjectName(QStringLiteral("detailCaption"));
    detailLayout->addWidget(argvCaption);
    detailArgv_ = new QPlainTextEdit(detailCard);
    detailArgv_->setObjectName(QStringLiteral("argvView"));
    detailArgv_->setReadOnly(true);
    detailArgv_->setMaximumHeight(110);
    detailArgv_->setPlaceholderText(
        QStringLiteral("Press Show argv (Ctrl+K) to print the exact apps.json argv."));
    applyMonospace(detailArgv_);
    detailLayout->addWidget(detailArgv_);

    auto* launchRow = new QHBoxLayout;
    launchRow->setSpacing(6);
    auto* timeoutCaption = new QLabel(QStringLiteral("Launch timeout (s)"), detailCard);
    timeoutCaption->setObjectName(QStringLiteral("muted"));
    launchRow->addWidget(timeoutCaption);
    launchTimeoutSpin_ = new QSpinBox(detailCard);
    launchTimeoutSpin_->setObjectName(QStringLiteral("launchTimeoutSpin"));
    launchTimeoutSpin_->setRange(1, 600);
    launchTimeoutSpin_->setValue(8);
    launchTimeoutSpin_->setToolTip(
        QStringLiteral("Passed to the core's launchPackage; on timeout the process is terminated "
                       "and then killed."));
    launchRow->addWidget(launchTimeoutSpin_);
    launchRow->addStretch(1);
    detailLayout->addLayout(launchRow);

    launchButton_ = new QPushButton(QStringLiteral("Test launch"), detailCard);
    launchButton_->setObjectName(QStringLiteral("testLaunchButton"));
    launchButton_->setAccessibleName(QStringLiteral("Test launch the selected package"));
    launchButton_->setToolTip(
        QStringLiteral("Calls the core's launchPackage: starts the recorded executable and argv, "
                       "reports pid, exit code, timeout, or spawn error. It does not inject into a "
                       "running XMB."));
    argvButton_ = new QPushButton(QStringLiteral("Show argv"), detailCard);
    argvButton_->setObjectName(QStringLiteral("showArgvButton"));
    argvButton_->setToolTip(
        QStringLiteral("Calls packageArgv and prints the exact apps.json argv (Ctrl+K)"));
    verifyButton_ = new QPushButton(QStringLiteral("Verify"), detailCard);
    verifyButton_->setObjectName(QStringLiteral("verifyHashButton"));
    verifyButton_->setToolTip(
        QStringLiteral("Recompute this package's ROM hash with verifyPackageHashes"));
    editButton_ = new QPushButton(QStringLiteral("Edit"), detailCard);
    editButton_->setObjectName(QStringLiteral("editPackageButton"));
    editButton_->setToolTip(
        QStringLiteral("Edit title, collection, emulator, and extra arguments (Ctrl+E)"));
    editTagsButton_ = new QPushButton(QStringLiteral("Edit tags"), detailCard);
    editTagsButton_->setObjectName(QStringLiteral("editTagsButton"));
    editTagsButton_->setToolTip(QStringLiteral("Edit tags through updatePackageTags"));
    relinkButton_ = new QPushButton(QStringLiteral("Relink..."), detailCard);
    relinkButton_->setObjectName(QStringLiteral("relinkButton"));
    relinkButton_->setToolTip(
        QStringLiteral("Point this package at a moved ROM through relinkPackage"));
    reimportButton_ = new QPushButton(QStringLiteral("Re-import"), detailCard);
    reimportButton_->setObjectName(QStringLiteral("reimportButton"));
    reimportButton_->setToolTip(QStringLiteral("Rebuild this package from its ROM"));
    removeButton_ = new QPushButton(QStringLiteral("Remove"), detailCard);
    removeButton_->setObjectName(QStringLiteral("dangerButton"));
    removeButton_->setToolTip(QStringLiteral("Remove the package and its registration"));
    copyArgvButton_ = new QPushButton(QStringLiteral("Copy argv"), detailCard);
    copyArgvButton_->setObjectName(QStringLiteral("copyArgvButton"));
    copyArgvButton_->setToolTip(QStringLiteral("Copy the displayed argv to the clipboard"));

    auto* buttons = new QGridLayout;
    buttons->setHorizontalSpacing(4);
    buttons->setVerticalSpacing(4);
    buttons->addWidget(launchButton_, 0, 0, 1, 2);
    buttons->addWidget(argvButton_, 0, 2);
    buttons->addWidget(verifyButton_, 0, 3);
    buttons->addWidget(editButton_, 1, 0);
    buttons->addWidget(editTagsButton_, 1, 1);
    buttons->addWidget(relinkButton_, 1, 2);
    buttons->addWidget(copyArgvButton_, 1, 3);
    buttons->addWidget(reimportButton_, 2, 0);
    buttons->addWidget(removeButton_, 2, 1);
    for (int column = 0; column < 4; ++column)
        buttons->setColumnStretch(column, 1);
    detailLayout->addLayout(buttons);

    launchResult_ = makeMutedLabel(
        detailCard, QStringLiteral("Test launch calls launchPackage and reports its outcome."));
    launchResult_->setObjectName(QStringLiteral("launchResult"));
    detailLayout->addWidget(launchResult_);
    detailLayout->addStretch(1);
    detailScroll->setWidget(detailCard);

    librarySplitter_->addWidget(collectionsCard);
    librarySplitter_->addWidget(tableHost);
    librarySplitter_->addWidget(detailScroll);
    librarySplitter_->setStretchFactor(0, 0);
    librarySplitter_->setStretchFactor(1, 3);
    librarySplitter_->setStretchFactor(2, 2);
    librarySplitter_->setSizes({220, 640, 460});
    tablePageLayout->addWidget(librarySplitter_, 1);
    libraryStack_->addWidget(libraryTablePage_);
    layout->addWidget(libraryStack_, 1);

    connect(librarySearch_, &QLineEdit::textChanged, this,
            [this](const QString&) { applyLibraryFilter(); });
    connect(groupCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        groupMode_ = index;
        applyLibraryFilter();
    });
    connect(clearFiltersButton, &QPushButton::clicked, this, &MainWindow::clearLibraryFilters);
    connect(libraryTable_, &QTableWidget::itemSelectionChanged, this,
            [this] { updateLibrarySelection(); });
    connect(libraryTable_, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { editLibraryEntry(); });
    connect(libraryTable_, &QTableWidget::customContextMenuRequested, this,
            &MainWindow::showLibraryContextMenu);
    connect(launchButton_, &QPushButton::clicked, this, &MainWindow::launchLibraryEntry);
    connect(argvButton_, &QPushButton::clicked, this, &MainWindow::showArgvForSelected);
    connect(copyArgvButton_, &QPushButton::clicked, this, &MainWindow::copyArgv);
    connect(verifyButton_, &QPushButton::clicked, this, &MainWindow::verifySelectedHashes);
    connect(editButton_, &QPushButton::clicked, this, &MainWindow::editLibraryEntry);
    connect(editTagsButton_, &QPushButton::clicked, this, &MainWindow::editLibraryTags);
    connect(relinkButton_, &QPushButton::clicked, this, &MainWindow::relinkSelectedPackage);
    connect(removeButton_, &QPushButton::clicked, this, &MainWindow::removeLibraryEntry);
    connect(reimportButton_, &QPushButton::clicked, this, &MainWindow::reimportLibraryEntry);
    connect(collectionsList_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::onCollectionFilterChanged);
    connect(collectionAddButton_, &QPushButton::clicked, this, &MainWindow::createCollection);
    connect(collectionRenameButton_, &QPushButton::clicked, this,
            &MainWindow::renameSelectedCollection);
    connect(collectionRemoveButton_, &QPushButton::clicked, this,
            &MainWindow::removeSelectedCollection);
    connect(bulkCollectionButton_, &QPushButton::clicked, this,
            &MainWindow::bulkSetCollection);
    connect(bulkEmulatorButton_, &QPushButton::clicked, this, &MainWindow::bulkSetEmulator);
    connect(bulkAddTagButton_, &QPushButton::clicked, this, &MainWindow::bulkAddTags);
    connect(bulkRemoveTagButton_, &QPushButton::clicked, this, &MainWindow::bulkRemoveTags);
    connect(bulkRemoveButton_, &QPushButton::clicked, this, &MainWindow::bulkRemovePackages);
    connect(bulkClearButton_, &QPushButton::clicked, this, [this] {
        libraryTable_->clearSelection();
        updateLibrarySelection();
    });
    connect(goImportButton, &QPushButton::clicked, this, [this] {
        if (tabs_ && importTab_)
            tabs_->setCurrentWidget(importTab_);
    });
    connect(addNowButton, &QPushButton::clicked, this, &MainWindow::addFiles);

    updateLibraryActions();
    updateBulkBar();
    return page;
}

QWidget* MainWindow::buildPlatformMapTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(6);
    topRow->addWidget(makeSectionLabel(page, QStringLiteral("Platform map")));
    mappingStatus_ = makeMutedLabel(page, QString());
    topRow->addWidget(mappingStatus_, 1);
    mappingRefreshButton_ = new QPushButton(QStringLiteral("Reload"), page);
    mappingRefreshButton_->setObjectName(QStringLiteral("mappingRefreshButton"));
    mappingRefreshButton_->setToolTip(
        QStringLiteral("Re-read platform-map.json and every emulator profile"));
    mappingSaveButton_ = new QPushButton(QStringLiteral("Save map"), page);
    mappingSaveButton_->setObjectName(QStringLiteral("mappingSaveButton"));
    mappingSaveButton_->setAccessibleName(QStringLiteral("Save the platform map"));
    mappingSaveButton_->setToolTip(
        QStringLiteral("Validate and write platform-map.json through savePlatformMap"));
    mappingRevertButton_ = new QPushButton(QStringLiteral("Revert"), page);
    mappingRevertButton_->setObjectName(QStringLiteral("mappingRevertButton"));
    mappingRevertButton_->setToolTip(QStringLiteral("Discard unsaved edits and reload the map"));
    topRow->addWidget(mappingRefreshButton_);
    topRow->addWidget(mappingRevertButton_);
    topRow->addWidget(mappingSaveButton_);
    layout->addLayout(topRow);

    mappingBanner_ = new QFrame(page);
    mappingBanner_->setObjectName(QStringLiteral("warningBanner"));
    auto* bannerLayout = new QHBoxLayout(mappingBanner_);
    bannerLayout->setContentsMargins(10, 6, 10, 6);
    bannerLayout->setSpacing(8);
    auto* bannerTitle = new QLabel(QStringLiteral("Unmapped platforms"), mappingBanner_);
    bannerTitle->setObjectName(QStringLiteral("warningText"));
    bannerLayout->addWidget(bannerTitle);
    mappingBannerLabel_ = new QLabel(mappingBanner_);
    mappingBannerLabel_->setObjectName(QStringLiteral("warningText"));
    mappingBannerLabel_->setWordWrap(true);
    mappingBannerLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bannerLayout->addWidget(mappingBannerLabel_, 1);
    mappingBanner_->setVisible(false);
    layout->addWidget(mappingBanner_);

    mappingFileStateLabel_ = makeMutedLabel(page, QString());
    layout->addWidget(mappingFileStateLabel_);

    auto* body = new QSplitter(Qt::Horizontal, page);
    body->setChildrenCollapsible(false);
    mappingTable_ = new QTableWidget(0, 4, body);
    mappingTable_->setObjectName(QStringLiteral("mappingTable"));
    mappingTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Platform"), QStringLiteral("Emulator"),
         QStringLiteral("Argument override"), QStringLiteral("Source")});
    mappingTable_->verticalHeader()->setVisible(false);
    mappingTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    mappingTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    mappingTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mappingTable_->setAlternatingRowColors(true);
    mappingTable_->setShowGrid(false);
    mappingTable_->horizontalHeader()->setHighlightSections(false);
    mappingTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mappingTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    mappingTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    mappingTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    mappingTable_->setToolTip(
        QStringLiteral("Every platform the scanner knows, its resolved emulator, and where the "
                       "answer came from. The emulator column is editable."));
    mappingTable_->setAccessibleName(QStringLiteral("Platform to emulator map"));
    body->addWidget(mappingTable_);

    auto* editorCard = makeCard(body);
    auto* editorLayout = new QVBoxLayout(editorCard);
    editorLayout->setContentsMargins(14, 12, 14, 12);
    editorLayout->setSpacing(6);
    mappingEditorTitle_ = makeSectionLabel(editorCard, QStringLiteral("Select a platform"));
    editorLayout->addWidget(mappingEditorTitle_);
    editorLayout->addWidget(makeMutedLabel(
        editorCard,
        QStringLiteral("An explicit mapping wins even when the profile does not list the "
                       "platform. Choosing (unmapped) removes the entry, which falls back to the "
                       "first profile that lists the platform.")));
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    mappingEmulatorCombo_ = new QComboBox(editorCard);
    mappingEmulatorCombo_->setObjectName(QStringLiteral("mappingEmulatorCombo"));
    form->addRow(QStringLiteral("Emulator"), mappingEmulatorCombo_);
    editorLayout->addLayout(form);

    auto* overrideCaption = new QLabel(QStringLiteral("Argument override"), editorCard);
    overrideCaption->setObjectName(QStringLiteral("detailCaption"));
    editorLayout->addWidget(overrideCaption);
    mappingArgumentsEdit_ = new QPlainTextEdit(editorCard);
    mappingArgumentsEdit_->setObjectName(QStringLiteral("mappingArgumentsEdit"));
    mappingArgumentsEdit_->setPlaceholderText(
        QStringLiteral("One argv element per line, e.g.\n-L\nsnes9x_libretro\n{rom}\n\nLeave "
                       "empty to use the profile's argument template."));
    mappingArgumentsEdit_->setTabChangesFocus(true);
    applyMonospace(mappingArgumentsEdit_);
    editorLayout->addWidget(mappingArgumentsEdit_, 1);
    mappingEditorHint_ = makeMutedLabel(
        editorCard,
        QStringLiteral("Placeholders: {rom}, {title}, {id}, {dir}, {core}. Each line becomes "
                       "exactly one argv element; there is no whitespace splitting, quoting, or "
                       "shell interpretation."));
    editorLayout->addWidget(mappingEditorHint_);
    auto* overrideButtons = new QHBoxLayout;
    mappingClearOverrideButton_ = new QPushButton(QStringLiteral("Clear override"), editorCard);
    mappingClearOverrideButton_->setToolTip(
        QStringLiteral("Drop the argument override so the profile template applies"));
    overrideButtons->addWidget(mappingClearOverrideButton_);
    overrideButtons->addStretch(1);
    editorLayout->addLayout(overrideButtons);
    body->addWidget(editorCard);
    body->setStretchFactor(0, 3);
    body->setStretchFactor(1, 2);
    layout->addWidget(body, 1);

    connect(mappingTable_, &QTableWidget::itemSelectionChanged, this,
            &MainWindow::onMappingSelectionChanged);
    connect(mappingEmulatorCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { applyMappingEditor(); });
    connect(mappingArgumentsEdit_, &QPlainTextEdit::textChanged, this,
            [this] { applyMappingEditor(); });
    connect(mappingClearOverrideButton_, &QPushButton::clicked, this,
            &MainWindow::clearMappingOverride);
    connect(mappingSaveButton_, &QPushButton::clicked, this, &MainWindow::saveMappingTable);
    connect(mappingRevertButton_, &QPushButton::clicked, this, &MainWindow::revertMappingTable);
    connect(mappingRefreshButton_, &QPushButton::clicked, this, [this] {
        refreshPlatformMap();
        setStatusMessage(QStringLiteral("Platform map reloaded."));
    });

    return page;
}

QWidget* MainWindow::buildImportTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(8);
    topRow->addWidget(makeSectionLabel(page, QStringLiteral("Import")));
    importCountLabel_ = makeMutedLabel(page, QStringLiteral("0 detected games"));
    importCountLabel_->setWordWrap(false);
    importCountLabel_->setMinimumWidth(190);
    topRow->addWidget(importCountLabel_);
    topRow->addStretch(1);
    importMessage_ = makeMutedLabel(page, QString());
    topRow->addWidget(importMessage_, 1);
    layout->addLayout(topRow);

    auto* zone = new DropZone(page);
    importDropZone_ = zone;
    zone->onPathsDropped = [this](const QStringList& paths) { addScannedPaths(paths); };
    layout->addWidget(zone);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(6);
    addFilesButton_ = new QPushButton(QStringLiteral("Add files..."), page);
    addFilesButton_->setToolTip(QStringLiteral("Choose ROM or disc-image files (Ctrl+O)"));
    addFolderButton_ = new QPushButton(QStringLiteral("Add folder..."), page);
    addFolderButton_->setToolTip(QStringLiteral("Scan a folder recursively (Ctrl+Shift+O)"));
    importPkgButton_ = new QPushButton(QStringLiteral("Import real .pkg..."), page);
    importPkgButton_->setToolTip(
        QStringLiteral("Probe a PS3 .pkg. Encrypted retail packages are refused honestly."));
    selectAllButton_ = new QPushButton(QStringLiteral("Select all"), page);
    selectNoneButton_ = new QPushButton(QStringLiteral("Select none"), page);
    clearImportButton_ = new QPushButton(QStringLiteral("Clear list"), page);
    buttonRow->addWidget(addFilesButton_);
    buttonRow->addWidget(addFolderButton_);
    buttonRow->addWidget(importPkgButton_);
    buttonRow->addStretch(1);
    buttonRow->addWidget(selectAllButton_);
    buttonRow->addWidget(selectNoneButton_);
    buttonRow->addWidget(clearImportButton_);
    layout->addLayout(buttonRow);

    importTable_ = new QTableWidget(0, 7, page);
    importTable_->setObjectName(QStringLiteral("importTable"));
    importTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Title"), QStringLiteral("Platform"), QStringLiteral("Kind"),
         QStringLiteral("Size"), QStringLiteral("ROM path"), QStringLiteral("Emulator"),
         QStringLiteral("Result")});
    importTable_->verticalHeader()->setVisible(false);
    importTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    importTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    importTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    importTable_->setAlternatingRowColors(true);
    importTable_->setShowGrid(false);
    importTable_->horizontalHeader()->setHighlightSections(false);
    importTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    importTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    for (int column : {1, 2, 3, 5})
        importTable_->horizontalHeader()->setSectionResizeMode(column,
                                                               QHeaderView::ResizeToContents);
    importTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Interactive);
    importTable_->setColumnWidth(6, 300);
    importTable_->setAccessibleName(QStringLiteral("Detected games"));
    layout->addWidget(importTable_, 1);

    auto* importRow = new QHBoxLayout;
    importRow->setSpacing(8);
    auto* collectionCaption = new QLabel(QStringLiteral("Collection"), page);
    collectionCaption->setObjectName(QStringLiteral("muted"));
    importRow->addWidget(collectionCaption);
    importCollectionEdit_ = new QLineEdit(page);
    importCollectionEdit_->setObjectName(QStringLiteral("importCollectionEdit"));
    importCollectionEdit_->setPlaceholderText(QStringLiteral("Optional; applied to every import"));
    importCollectionEdit_->setMaximumWidth(280);
    importCollectionEdit_->setToolTip(
        QStringLiteral("Written to collections.json and pkg.json for each imported game"));
    importRow->addWidget(importCollectionEdit_);
    importRow->addStretch(1);
    importProgress_ = new QProgressBar(page);
    importProgress_->setFixedWidth(220);
    importProgress_->setVisible(false);
    importRow->addWidget(importProgress_);
    importButton_ = new QPushButton(QStringLiteral("Import selected"), page);
    importButton_->setObjectName(QStringLiteral("accentButton"));
    importButton_->setAccessibleName(QStringLiteral("Import selected"));
    importButton_->setToolTip(QStringLiteral("Create a virtual package for every checked game"));
    importRow->addWidget(importButton_);
    layout->addLayout(importRow);

    auto* reportCaption = new QLabel(QStringLiteral("Import report"), page);
    reportCaption->setObjectName(QStringLiteral("muted"));
    layout->addWidget(reportCaption);
    importReport_ = new QPlainTextEdit(page);
    importReport_->setObjectName(QStringLiteral("importReport"));
    importReport_->setReadOnly(true);
    importReport_->setMaximumHeight(96);
    importReport_->setPlaceholderText(
        QStringLiteral("Per-game results and the core's exact error messages appear here."));
    layout->addWidget(importReport_);

    connect(addFilesButton_, &QPushButton::clicked, this, &MainWindow::addFiles);
    connect(addFolderButton_, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(importPkgButton_, &QPushButton::clicked, this, &MainWindow::importRealPkg);
    connect(clearImportButton_, &QPushButton::clicked, this, [this] {
        importRows_.clear();
        rebuildImportTable();
        importMessage_->clear();
        importCountLabel_->setText(QStringLiteral("0 detected games"));
    });
    connect(selectAllButton_, &QPushButton::clicked, this, [this] {
        for (ImportRow& row : importRows_)
            row.selected = true;
        rebuildImportTable();
    });
    connect(selectNoneButton_, &QPushButton::clicked, this, [this] {
        for (ImportRow& row : importRows_)
            row.selected = false;
        rebuildImportTable();
    });
    connect(importButton_, &QPushButton::clicked, this, &MainWindow::importSelected);
    connect(importTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (rebuildingImportTable_ || !item || item->column() != 0)
            return;
        const int row = item->row();
        if (row < 0 || row >= importRows_.size())
            return;
        importRows_[row].selected = item->checkState() == Qt::Checked;
        refreshImportSelectionState();
    });

    return page;
}

QWidget* MainWindow::buildEmulatorsTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(6);
    topRow->addWidget(makeSectionLabel(page, QStringLiteral("Emulators")));
    emulatorStatus_ = makeMutedLabel(page, QString());
    topRow->addWidget(emulatorStatus_, 1);
    emulatorAddButton_ = new QPushButton(QStringLiteral("Add profile"), page);
    emulatorAddButton_->setToolTip(QStringLiteral("Create a user emulator profile"));
    emulatorSaveButton_ = new QPushButton(QStringLiteral("Save profile"), page);
    emulatorSaveButton_->setObjectName(QStringLiteral("accentButton"));
    emulatorSaveButton_->setToolTip(
        QStringLiteral("Write this profile to emulators.json (also updates built-in paths)"));
    emulatorRemoveButton_ = new QPushButton(QStringLiteral("Remove"), page);
    emulatorRemoveButton_->setToolTip(QStringLiteral("Remove the selected user profile"));
    emulatorDetectButton_ = new QPushButton(QStringLiteral("Detect installed"), page);
    emulatorDetectButton_->setToolTip(
        QStringLiteral("Search known install locations and the uninstall registry"));
    topRow->addWidget(emulatorAddButton_);
    topRow->addWidget(emulatorSaveButton_);
    topRow->addWidget(emulatorRemoveButton_);
    topRow->addWidget(emulatorDetectButton_);
    layout->addLayout(topRow);

    emulatorTable_ = new QTableWidget(0, 5, page);
    emulatorTable_->setObjectName(QStringLiteral("emulatorTable"));
    emulatorTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Origin"), QStringLiteral("Name"), QStringLiteral("Platforms"),
         QStringLiteral("Executable"), QStringLiteral("Argument template")});
    emulatorTable_->verticalHeader()->setVisible(false);
    emulatorTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    emulatorTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    emulatorTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    emulatorTable_->setAlternatingRowColors(true);
    emulatorTable_->setShowGrid(false);
    emulatorTable_->horizontalHeader()->setHighlightSections(false);
    emulatorTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    emulatorTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    emulatorTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    emulatorTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    emulatorTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    emulatorTable_->setAccessibleName(QStringLiteral("Emulator profiles"));
    layout->addWidget(emulatorTable_, 3);

    auto* body = new QHBoxLayout;
    body->setSpacing(10);
    auto* formCard = makeCard(page);
    auto* form = new QFormLayout(formCard);
    form->setContentsMargins(14, 12, 14, 12);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    emulatorIdEdit_ = new QLineEdit(formCard);
    emulatorIdEdit_->setObjectName(QStringLiteral("emulatorIdEdit"));
    emulatorIdEdit_->setPlaceholderText(QStringLiteral("my-emulator"));
    emulatorIdEdit_->setToolTip(
        QStringLiteral("Stable profile id: ASCII letters, digits, '_', '.', '-'"));
    emulatorNameEdit_ = new QLineEdit(formCard);
    emulatorNameEdit_->setObjectName(QStringLiteral("emulatorNameEdit"));
    emulatorPlatformsEdit_ = new QLineEdit(formCard);
    emulatorPlatformsEdit_->setObjectName(QStringLiteral("emulatorPlatformsEdit"));
    emulatorPlatformsEdit_->setPlaceholderText(QStringLiteral("PS3, PS2"));
    emulatorPlatformsEdit_->setToolTip(
        QStringLiteral("Comma-separated platform names, matching the scanner's platform names"));
    emulatorTemplateEdit_ = new QLineEdit(formCard);
    emulatorTemplateEdit_->setObjectName(QStringLiteral("emulatorTemplateEdit"));
    emulatorTemplateEdit_->setPlaceholderText(QStringLiteral("{rom}"));
    emulatorTemplateEdit_->setToolTip(
        QStringLiteral("Placeholders: {rom}, {title}, {id}, {dir}, {core}. Quoting groups one "
                       "argument; no shell interpretation."));
    emulatorEnvEdit_ = new QPlainTextEdit(formCard);
    emulatorEnvEdit_->setObjectName(QStringLiteral("emulatorEnvEdit"));
    emulatorEnvEdit_->setPlaceholderText(QStringLiteral("NAME=value per line"));
    emulatorEnvEdit_->setMaximumHeight(64);
    emulatorEnvEdit_->setTabChangesFocus(true);

    auto* executableRow = new QWidget(formCard);
    auto* executableLayout = new QHBoxLayout(executableRow);
    executableLayout->setContentsMargins(0, 0, 0, 0);
    executableLayout->setSpacing(6);
    emulatorExecutableEdit_ = new QLineEdit(executableRow);
    emulatorExecutableEdit_->setObjectName(QStringLiteral("emulatorExecutableEdit"));
    emulatorExecutableEdit_->setPlaceholderText(QStringLiteral("C:/Emulators/RPCS3/rpcs3.exe"));
    executableLayout->addWidget(emulatorExecutableEdit_, 1);
    auto* executableBrowse = new QPushButton(QStringLiteral("Browse..."), executableRow);
    executableBrowse->setToolTip(QStringLiteral("Choose the emulator executable"));
    executableLayout->addWidget(executableBrowse);

    auto* workingRow = new QWidget(formCard);
    auto* workingLayout = new QHBoxLayout(workingRow);
    workingLayout->setContentsMargins(0, 0, 0, 0);
    workingLayout->setSpacing(6);
    emulatorWorkingDirectoryEdit_ = new QLineEdit(workingRow);
    emulatorWorkingDirectoryEdit_->setObjectName(
        QStringLiteral("emulatorWorkingDirectoryEdit"));
    emulatorWorkingDirectoryEdit_->setPlaceholderText(
        QStringLiteral("Defaults to the executable folder"));
    workingLayout->addWidget(emulatorWorkingDirectoryEdit_, 1);
    auto* workingBrowse = new QPushButton(QStringLiteral("Browse..."), workingRow);
    workingBrowse->setToolTip(QStringLiteral("Choose the working directory"));
    workingLayout->addWidget(workingBrowse);

    form->addRow(QStringLiteral("Id"), emulatorIdEdit_);
    form->addRow(QStringLiteral("Name"), emulatorNameEdit_);
    form->addRow(QStringLiteral("Executable"), executableRow);
    form->addRow(QStringLiteral("Working directory"), workingRow);
    form->addRow(QStringLiteral("Platforms"), emulatorPlatformsEdit_);
    form->addRow(QStringLiteral("Argument template"), emulatorTemplateEdit_);
    form->addRow(QStringLiteral("Environment"), emulatorEnvEdit_);
    body->addWidget(formCard, 3);

    auto* previewCard = makeCard(page);
    auto* previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(14, 12, 14, 12);
    previewLayout->setSpacing(6);
    previewLayout->addWidget(makeSectionLabel(previewCard, QStringLiteral("Argument preview")));
    previewLayout->addWidget(makeMutedLabel(
        previewCard,
        QStringLiteral("The preview runs expandArguments() with a sample path, so the quoting "
                       "rule is visible before anything is launched.")));
    auto* sampleCaption = new QLabel(QStringLiteral("Sample ROM path"), previewCard);
    sampleCaption->setObjectName(QStringLiteral("muted"));
    previewLayout->addWidget(sampleCaption);
    auto* sampleRow = new QHBoxLayout;
    sampleRow->setSpacing(6);
    emulatorSampleRomEdit_ = new QLineEdit(previewCard);
    emulatorSampleRomEdit_->setObjectName(QStringLiteral("emulatorSampleRomEdit"));
    emulatorSampleRomEdit_->setToolTip(
        QStringLiteral("Used only for the preview and Test launch"));
    sampleRow->addWidget(emulatorSampleRomEdit_, 1);
    auto* sampleBrowse = new QPushButton(QStringLiteral("Browse..."), previewCard);
    sampleBrowse->setToolTip(QStringLiteral("Choose a sample ROM"));
    sampleRow->addWidget(sampleBrowse);
    previewLayout->addLayout(sampleRow);
    emulatorPreview_ = new QPlainTextEdit(previewCard);
    emulatorPreview_->setObjectName(QStringLiteral("emulatorPreview"));
    emulatorPreview_->setReadOnly(true);
    emulatorPreview_->setMaximumHeight(64);
    emulatorPreview_->setToolTip(QStringLiteral("argv after expansion; never passed to a shell"));
    previewLayout->addWidget(emulatorPreview_);
    emulatorPreviewError_ = makeErrorLabel(previewCard);
    previewLayout->addWidget(emulatorPreviewError_);
    emulatorTestButton_ = new QPushButton(QStringLiteral("Test launch (sample argv)"), previewCard);
    emulatorTestButton_->setObjectName(QStringLiteral("emulatorTestButton"));
    emulatorTestButton_->setToolTip(
        QStringLiteral("Start the emulator with the sample argv from this form and report the "
                       "process result. Package launches use the library's Test launch, which "
                       "calls the core's launchPackage."));
    previewLayout->addWidget(emulatorTestButton_);
    emulatorLaunchResult_ = makeMutedLabel(previewCard, QString());
    previewLayout->addWidget(emulatorLaunchResult_);
    auto* placeholderHint = makeMutedLabel(
        previewCard, QStringLiteral("Placeholders: {rom}, {title}, {id}, {dir}, {core}"));
    previewLayout->addWidget(placeholderHint);
    previewLayout->addStretch(1);
    body->addWidget(previewCard, 2);
    layout->addLayout(body, 2);

    auto* evidenceCaption = makeMutedLabel(
        page, QStringLiteral("Detection evidence (core detectInstalled notes)"));
    layout->addWidget(evidenceCaption);
    emulatorEvidence_ = new QPlainTextEdit(page);
    emulatorEvidence_->setObjectName(QStringLiteral("emulatorEvidence"));
    emulatorEvidence_->setReadOnly(true);
    emulatorEvidence_->setMaximumHeight(82);
    emulatorEvidence_->setPlaceholderText(
        QStringLiteral("Run Detect installed to see the path and evidence for each emulator."));
    layout->addWidget(emulatorEvidence_, 1);

    connect(executableBrowse, &QPushButton::clicked, this, &MainWindow::browseForExecutable);
    connect(workingBrowse, &QPushButton::clicked, this, [this] {
        const QString start = emulatorWorkingDirectoryEdit_->text().trimmed().isEmpty()
            ? QFileInfo(emulatorExecutableEdit_->text()).absolutePath()
            : emulatorWorkingDirectoryEdit_->text().trimmed();
        const QString directory = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Choose the working directory"), start);
        if (!directory.isEmpty()) {
            emulatorWorkingDirectoryEdit_->setText(QDir::toNativeSeparators(directory));
            updateArgumentPreview();
        }
    });
    connect(sampleBrowse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("Choose a sample ROM"), lastBrowseDirectory_, romFilter());
        if (!file.isEmpty()) {
            emulatorSampleRomEdit_->setText(QDir::toNativeSeparators(file));
            lastBrowseDirectory_ = QFileInfo(file).absolutePath();
            setUiValue(QStringLiteral("paths/lastBrowse"), lastBrowseDirectory_);
            updateArgumentPreview();
        }
    });
    connect(emulatorTable_, &QTableWidget::itemSelectionChanged, this,
            &MainWindow::onEmulatorSelectionChanged);
    connect(emulatorAddButton_, &QPushButton::clicked, this,
            &MainWindow::addEmulatorProfile);
    connect(emulatorSaveButton_, &QPushButton::clicked, this, &MainWindow::saveEmulatorProfile);
    connect(emulatorRemoveButton_, &QPushButton::clicked, this,
            &MainWindow::removeEmulatorProfile);
    connect(emulatorDetectButton_, &QPushButton::clicked, this, [this] { runDetection(true); });
    connect(emulatorTestButton_, &QPushButton::clicked, this, &MainWindow::testLaunchEmulator);
    connect(emulatorIdEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateArgumentPreview(); });
    connect(emulatorPlatformsEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateArgumentPreview(); });
    connect(emulatorTemplateEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateArgumentPreview(); });
    connect(emulatorSampleRomEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateArgumentPreview(); });
    connect(emulatorExecutableEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateArgumentPreview(); });

    return page;
}

QWidget* MainWindow::buildDiagnosticsTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(6);
    topRow->addWidget(makeSectionLabel(page, QStringLiteral("Diagnostics")));
    diagnosticsMessage_ = makeMutedLabel(page, QString());
    topRow->addWidget(diagnosticsMessage_, 1);
    hashVerifyButton_ = new QPushButton(QStringLiteral("Verify hashes"), page);
    hashVerifyButton_->setToolTip(
        QStringLiteral("Run verifyPackageHashes across the library (Ctrl+Shift+H)"));
    mappingGapButton_ = new QPushButton(QStringLiteral("Mapping gaps"), page);
    mappingGapButton_->setToolTip(
        QStringLiteral("Show every platform with no mapping and no profile fallback"));
    auto* validateButton = new QPushButton(QStringLiteral("Validate now"), page);
    validateButton->setToolTip(
        QStringLiteral("Re-read apps.json, pkg/, emulators.json, collections.json, and the map"));
    auto* copyButton = new QPushButton(QStringLiteral("Copy report"), page);
    copyButton->setToolTip(QStringLiteral("Copy the full diagnostics text to the clipboard"));
    auto* openButton = new QPushButton(QStringLiteral("Open data folder"), page);
    topRow->addWidget(hashVerifyButton_);
    topRow->addWidget(mappingGapButton_);
    topRow->addWidget(validateButton);
    topRow->addWidget(copyButton);
    topRow->addWidget(openButton);
    layout->addLayout(topRow);

    auto* reportRow = new QHBoxLayout;
    reportRow->setSpacing(6);
    auto* reportCaption = new QLabel(QStringLiteral("Report path (xmb-manager-report/2)"), page);
    reportCaption->setObjectName(QStringLiteral("muted"));
    reportRow->addWidget(reportCaption);
    reportPathEdit_ = new QLineEdit(page);
    reportPathEdit_->setObjectName(QStringLiteral("reportPathEdit"));
    reportPathEdit_->setReadOnly(true);
    reportPathEdit_->setToolTip(
        QStringLiteral("The CLI writes xmb-manager-report/2 here with --export-report. The core "
                       "exposes no report writer, so the UI shows the path instead of fabricating "
                       "the JSON."));
    reportRow->addWidget(reportPathEdit_, 1);
    exportReportPathButton_ = new QPushButton(QStringLiteral("Copy path"), page);
    exportReportPathButton_->setToolTip(QStringLiteral("Copy the report path to the clipboard"));
    reportRow->addWidget(exportReportPathButton_);
    layout->addLayout(reportRow);

    auto* splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setChildrenCollapsible(false);
    layoutTree_ = new QTreeWidget(splitter);
    layoutTree_->setObjectName(QStringLiteral("layoutTree"));
    layoutTree_->setHeaderLabels({QStringLiteral("Data layout"), QStringLiteral("State")});
    layoutTree_->setRootIsDecorated(true);
    layoutTree_->setAlternatingRowColors(true);
    layoutTree_->setAccessibleName(QStringLiteral("Manager data layout"));
    splitter->addWidget(layoutTree_);

    diagnosticsReport_ = new QPlainTextEdit(splitter);
    diagnosticsReport_->setObjectName(QStringLiteral("diagnosticsReport"));
    diagnosticsReport_->setReadOnly(true);
    diagnosticsReport_->setPlaceholderText(
        QStringLiteral("Registry, package, emulator, and collection validation appears here."));
    diagnosticsReport_->setAccessibleName(QStringLiteral("Diagnostics report"));
    splitter->addWidget(diagnosticsReport_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    connect(validateButton, &QPushButton::clicked, this, [this] {
        refreshDiagnostics();
        setStatusMessage(
            QStringLiteral("Validation finished: %1 problem(s).").arg(lastProblemCount_));
    });
    connect(copyButton, &QPushButton::clicked, this, &MainWindow::copyDiagnostics);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openDataFolder);
    connect(hashVerifyButton_, &QPushButton::clicked, this, &MainWindow::verifyLibraryHashes);
    connect(mappingGapButton_, &QPushButton::clicked, this, [this] {
        if (tabs_ && platformMapTab_)
            tabs_->setCurrentWidget(platformMapTab_);
        setStatusMessage(
            mappingGapCount_ == 0
                ? QStringLiteral("Every known platform resolves to an emulator.")
                : QStringLiteral("%1 platform(s) have no mapping and no profile fallback.")
                      .arg(mappingGapCount_));
    });
    connect(exportReportPathButton_, &QPushButton::clicked, this,
            &MainWindow::copyReportPath);
    return page;
}

QWidget* MainWindow::buildSettingsTab() {
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);
    outer->addWidget(makeSectionLabel(page, QStringLiteral("Settings")));

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(10);

    auto* dataCard = makeCard(host);
    auto* dataLayout = new QVBoxLayout(dataCard);
    dataLayout->setContentsMargins(16, 14, 16, 14);
    dataLayout->setSpacing(6);
    dataLayout->addWidget(makeSectionLabel(dataCard, QStringLiteral("Data folder")));
    dataLayout->addWidget(makeMutedLabel(
        dataCard,
        QStringLiteral("Stored in manager.ini next to the manager executable. Switching folders "
                       "does not move or delete packages; the XMB reads packages from the data "
                       "folder it is launched with.")));
    auto* settingsDataRow = new QHBoxLayout;
    settingsDataRow->setSpacing(6);
    settingsDataDirectoryEdit_ = new QLineEdit(dataCard);
    settingsDataDirectoryEdit_->setObjectName(QStringLiteral("settingsDataDirectoryEdit"));
    settingsDataDirectoryEdit_->setToolTip(QStringLiteral("Absolute path to the manager data folder"));
    settingsDataRow->addWidget(settingsDataDirectoryEdit_, 1);
    auto* settingsBrowse = new QPushButton(QStringLiteral("Browse..."), dataCard);
    auto* settingsApply = new QPushButton(QStringLiteral("Apply"), dataCard);
    settingsApply->setObjectName(QStringLiteral("accentButton"));
    settingsDataRow->addWidget(settingsBrowse);
    settingsDataRow->addWidget(settingsApply);
    dataLayout->addLayout(settingsDataRow);
    settingsMessage_ = makeErrorLabel(dataCard);
    dataLayout->addWidget(settingsMessage_);
    layout->addWidget(dataCard);

    auto* defaultsCard = makeCard(host);
    auto* defaultsLayout = new QVBoxLayout(defaultsCard);
    defaultsLayout->setContentsMargins(16, 14, 16, 14);
    defaultsLayout->setSpacing(6);
    defaultsLayout->addWidget(
        makeSectionLabel(defaultsCard, QStringLiteral("Default emulator per platform")));
    defaultsLayout->addWidget(makeMutedLabel(
        defaultsCard,
        QStringLiteral("The platform map (Platform map tab) is authoritative: it resolves the "
                       "emulator and any argument override first. This preference only preselects "
                       "a profile when the platform has no mapping and no profile fallback. "
                       "Profiles themselves live in emulators.json.")));
    defaultEmulatorHost_ = new QWidget(defaultsCard);
    defaultEmulatorForm_ = new QFormLayout(defaultEmulatorHost_);
    defaultEmulatorForm_->setContentsMargins(0, 0, 0, 0);
    defaultEmulatorForm_->setVerticalSpacing(5);
    defaultEmulatorForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    defaultsLayout->addWidget(defaultEmulatorHost_);
    layout->addWidget(defaultsCard);

    auto* artworkCard = makeCard(host);
    auto* artworkLayout = new QVBoxLayout(artworkCard);
    artworkLayout->setContentsMargins(16, 14, 16, 14);
    artworkLayout->setSpacing(6);
    artworkLayout->addWidget(makeSectionLabel(artworkCard, QStringLiteral("Artwork cache")));
    artworkPolicyCombo_ = new QComboBox(artworkCard);
    artworkPolicyCombo_->addItem(
        QStringLiteral("Core-managed: one ICON0.PNG per package (at most 512x288)"));
    artworkPolicyCombo_->setEnabled(false);
    artworkPolicyCombo_->setToolTip(
        QStringLiteral("The current manager core exposes no shared artwork cache policy"));
    artworkLayout->addWidget(artworkPolicyCombo_);
    artworkLayout->addWidget(makeMutedLabel(
        artworkCard,
        QStringLiteral("The core stores a single ICON0.PNG inside each pkg/<id>/ folder. There is "
                       "no cache size policy in the core, so this setting is informational.")));
    layout->addWidget(artworkCard);

    auto* resetCard = makeCard(host);
    auto* resetLayout = new QVBoxLayout(resetCard);
    resetLayout->setContentsMargins(16, 14, 16, 14);
    resetLayout->setSpacing(6);
    resetLayout->addWidget(makeSectionLabel(resetCard, QStringLiteral("Reset")));
    resetLayout->addWidget(makeMutedLabel(
        resetCard,
        QStringLiteral("Reset UI preferences (default emulators, collection field, sample ROM "
                       "path, and window layout). Packages, registrations, and emulator profiles "
                       "are not deleted.")));
    auto* resetButton = new QPushButton(QStringLiteral("Reset to defaults"), resetCard);
    resetButton->setToolTip(QStringLiteral("Reset manager-ui.ini preferences only"));
    resetLayout->addWidget(resetButton, 0, Qt::AlignLeft);
    layout->addWidget(resetCard);
    layout->addStretch(1);

    scroll->setWidget(host);
    outer->addWidget(scroll, 1);

    connect(settingsBrowse, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Choose the manager data folder"),
            settingsDataDirectoryEdit_->text().trimmed(), QFileDialog::ShowDirsOnly);
        if (!directory.isEmpty())
            settingsDataDirectoryEdit_->setText(QDir::toNativeSeparators(directory));
    });
    connect(settingsApply, &QPushButton::clicked, this,
            &MainWindow::applySettingsDataDirectory);
    connect(settingsDataDirectoryEdit_, &QLineEdit::returnPressed, this,
            &MainWindow::applySettingsDataDirectory);
    connect(resetButton, &QPushButton::clicked, this, &MainWindow::resetSettingsDefaults);

    return page;
}

void MainWindow::buildStatusBar() {
    statusBar()->setSizeGripEnabled(true);
    statusBusyLabel_ = new QLabel(statusBar());
    statusBusyLabel_->setObjectName(QStringLiteral("statusBusy"));
    statusBusyLabel_->setVisible(false);
    statusBar()->addPermanentWidget(statusBusyLabel_);
    statusBar()->showMessage(QStringLiteral("Ready."));
}

void MainWindow::connectShortcuts() {
    refreshAction_ = new QAction(this);
    refreshAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    refreshAction_->setToolTip(QStringLiteral("Refresh"));
    connect(refreshAction_, &QAction::triggered, this, [this] { refreshAll(true); });
    addAction(refreshAction_);
    auto* f5Action = new QAction(this);
    f5Action->setShortcut(QKeySequence(Qt::Key_F5));
    connect(f5Action, &QAction::triggered, this, [this] { refreshAll(true); });
    addAction(f5Action);

    auto* findAction = new QAction(this);
    findAction->setShortcut(QKeySequence::Find);
    connect(findAction, &QAction::triggered, this, [this] {
        if (tabs_ && libraryTab_)
            tabs_->setCurrentWidget(libraryTab_);
        librarySearch_->setFocus();
        librarySearch_->selectAll();
    });
    addAction(findAction);

    auto* removeShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), libraryTable_);
    removeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(removeShortcut, &QShortcut::activated, this, &MainWindow::removeLibraryEntry);

    // The bulk bar is reachable without the mouse once several rows are selected.
    auto* verifyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V),
                                         libraryTable_);
    verifyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(verifyShortcut, &QShortcut::activated, this, &MainWindow::verifySelectedHashes);
}

void MainWindow::restoreUiState() {
    const QByteArray geometry = uiValue(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const int tab = uiValue(QStringLiteral("window/tab"), 0).toInt();
    if (tabs_ && tab >= 0 && tab < tabs_->count())
        tabs_->setCurrentIndex(tab);
    const QByteArray splitter = uiValue(QStringLiteral("window/librarySplitter")).toByteArray();
    if (librarySplitter_ && !splitter.isEmpty())
        librarySplitter_->restoreState(splitter);
}

void MainWindow::saveUiState() {
    setUiValue(QStringLiteral("window/geometry"), saveGeometry());
    if (tabs_)
        setUiValue(QStringLiteral("window/tab"), tabs_->currentIndex());
    if (librarySplitter_)
        setUiValue(QStringLiteral("window/librarySplitter"), librarySplitter_->saveState());
    if (importCollectionEdit_)
        setUiValue(QStringLiteral("import/collection"), importCollectionEdit_->text());
    if (emulatorSampleRomEdit_)
        setUiValue(QStringLiteral("emulators/sampleRom"), emulatorSampleRomEdit_->text());
    if (groupCombo_)
        setUiValue(QStringLiteral("library/groupMode"), groupCombo_->currentIndex());
    if (launchTimeoutSpin_)
        setUiValue(QStringLiteral("library/launchTimeout"), launchTimeoutSpin_->value());
    setUiValue(QStringLiteral("library/activeTags"), activeTagFilter_);
    if (!collectionFilter_.isEmpty())
        setUiValue(QStringLiteral("library/collectionFilter"), collectionFilter_);
    else if (uiSettings_)
        uiSettings_->remove(QStringLiteral("library/collectionFilter"));
    if (!lastBrowseDirectory_.isEmpty())
        setUiValue(QStringLiteral("paths/lastBrowse"), lastBrowseDirectory_);
}

void MainWindow::loadUiPreferences() {
    if (!uiSettings_)
        return;
    const QStringList keys = uiSettings_->allKeys();
    for (const QString& key : keys) {
        if (!key.startsWith(QStringLiteral("defaultEmulator/")))
            continue;
        const QString platform = prefPlatformFromKey(
            key.mid(QStringLiteral("defaultEmulator/").size()));
        if (!platform.isEmpty())
            defaultEmulators_.insert(platform, uiSettings_->value(key).toString());
    }
    if (importCollectionEdit_) {
        const QString collection = uiValue(QStringLiteral("import/collection")).toString();
        if (!collection.isEmpty())
            importCollectionEdit_->setText(collection);
    }
    if (emulatorSampleRomEdit_) {
        const QString sample =
            uiValue(QStringLiteral("emulators/sampleRom"),
                    QStringLiteral("C:/Games/Sample Game.iso"))
                .toString();
        emulatorSampleRomEdit_->setText(sample);
    }
    lastBrowseDirectory_ = uiValue(QStringLiteral("paths/lastBrowse")).toString();
    if (groupCombo_) {
        const int mode = uiValue(QStringLiteral("library/groupMode"), 0).toInt();
        groupMode_ = (mode >= 0 && mode < groupCombo_->count()) ? mode : kGroupNone;
        QSignalBlocker blocker(groupCombo_);
        groupCombo_->setCurrentIndex(groupMode_);
    }
    if (launchTimeoutSpin_) {
        const int seconds = uiValue(QStringLiteral("library/launchTimeout"), 8).toInt();
        launchTimeoutSpin_->setValue(qBound(1, seconds, 600));
    }
    const QStringList tags = uiValue(QStringLiteral("library/activeTags")).toStringList();
    for (const QString& tag : tags) {
        if (!tag.trimmed().isEmpty() && !activeTagFilter_.contains(tag, Qt::CaseInsensitive))
            activeTagFilter_.append(tag);
    }
    collectionFilter_ = uiValue(QStringLiteral("library/collectionFilter")).toString();
}

namespace {

// The core's detectInstalled() only looks at known install roots and the
// uninstall registry. Portable emulators are often unpacked into an arbitrary
// top-level folder (for example C:\dev\rpcs3), which those roots miss. This
// small supplement checks the drive roots one directory deep and only reports
// an existing .exe whose name contains the profile id. It never invents an
// install and it only runs inside the UI detection worker.
QHash<QString, QString> findPortableEmulatorExecutables(const QVector<EmulatorProfile>& profiles,
                                                        QStringList* notes) {
    QHash<QString, QString> found;
    QStringList candidateDirectories;
    const QSet<QString> skipTopLevel = {
        QStringLiteral("windows"), QStringLiteral("program files"),
        QStringLiteral("program files (x86)"), QStringLiteral("programdata"),
        QStringLiteral("$recycle.bin"), QStringLiteral("system volume information"),
        QStringLiteral("recovery"), QStringLiteral("perflogs"), QStringLiteral("msocache"),
        QStringLiteral("config.msi")};
    for (const QFileInfo& drive : QDir::drives()) {
        const QDir root(drive.absoluteFilePath());
        const QFileInfoList tops =
            root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& top : tops) {
            if (skipTopLevel.contains(top.fileName().toLower()))
                continue;
            candidateDirectories.append(top.absoluteFilePath());
            const QFileInfoList children = QDir(top.absoluteFilePath())
                                               .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                              QDir::Name);
            for (const QFileInfo& child : children)
                candidateDirectories.append(child.absoluteFilePath());
            if (candidateDirectories.size() > 6000)
                break;
        }
        if (candidateDirectories.size() > 6000)
            break;
    }

    for (const EmulatorProfile& profile : profiles) {
        if (!profile.executable.trimmed().isEmpty())
            continue;
        const QString id = profile.id.toLower();
        if (id.isEmpty())
            continue;
        for (const QString& directory : candidateDirectories) {
            if (!QFileInfo(directory).fileName().toLower().contains(id))
                continue;
            const QStringList searchDirectories = {
                directory, QDir(directory).filePath(QStringLiteral("bin"))};
            for (const QString& searchDirectory : searchDirectories) {
                if (!QFileInfo(searchDirectory).isDir())
                    continue;
                const QFileInfoList executables =
                    QDir(searchDirectory)
                        .entryInfoList(QStringList{QStringLiteral("*.exe")}, QDir::Files,
                                       QDir::Name);
                for (const QFileInfo& executable : executables) {
                    if (!executable.completeBaseName().toLower().contains(id))
                        continue;
                    const QString path = QDir::cleanPath(executable.absoluteFilePath());
                    found.insert(profile.id, path);
                    if (notes) {
                        notes->append(QStringLiteral("%1: %2 (evidence: portable folder %3)")
                                          .arg(profile.id, path, directory));
                    }
                    break;
                }
                if (found.contains(profile.id))
                    break;
            }
            if (found.contains(profile.id))
                break;
        }
    }
    return found;
}

bool findRegisteredApp(const Settings& settings, const QString& id, ContentApp& app) {
    QVector<ContentApp> apps;
    QString error;
    if (!ContentRegistry::load(registryPath(settings), apps, error))
        return false;
    for (const ContentApp& candidate : apps) {
        if (candidate.id == id) {
            app = candidate;
            return true;
        }
    }
    return false;
}

} // namespace

bool MainWindow::isBusy() const {
    return scanRunning_ || importRunning_ || taskRunning_;
}

bool MainWindow::beginTask(const QString& name) {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan, import, or task to finish."));
        return false;
    }
    taskRunning_ = true;
    taskName_ = name;
    setHeaderNotice(QString());
    updateBusyUi();
    setStatusMessage(QStringLiteral("%1...").arg(name));
    return true;
}

void MainWindow::endTask() {
    taskRunning_ = false;
    taskName_.clear();
    updateBusyUi();
}

void MainWindow::waitForTasks() {
    QList<QThread*> threads;
    if (scanThread_)
        threads.append(scanThread_.data());
    if (importThread_)
        threads.append(importThread_.data());
    if (detectionThread_)
        threads.append(detectionThread_.data());
    if (taskThread_)
        threads.append(taskThread_.data());
    for (QThread* thread : threads) {
        if (!thread || !thread->isRunning())
            continue;
        if (!thread->wait(20000)) {
            thread->terminate();
            thread->wait(2000);
        }
    }
}

void MainWindow::updateBusyUi() {
    const bool busy = isBusy();
    if (refreshButton_)
        refreshButton_->setEnabled(!busy && !detectionRunning_);
    if (refreshAction_)
        refreshAction_->setEnabled(!busy && !detectionRunning_);
    if (addFilesButton_)
        addFilesButton_->setEnabled(!busy);
    if (addFolderButton_)
        addFolderButton_->setEnabled(!busy);
    if (importPkgButton_)
        importPkgButton_->setEnabled(!busy);
    if (clearImportButton_)
        clearImportButton_->setEnabled(!busy);
    if (selectAllButton_)
        selectAllButton_->setEnabled(!busy);
    if (selectNoneButton_)
        selectNoneButton_->setEnabled(!busy);
    if (browseDataButton_)
        browseDataButton_->setEnabled(!busy);
    if (emulatorAddButton_)
        emulatorAddButton_->setEnabled(!busy);
    if (emulatorSaveButton_)
        emulatorSaveButton_->setEnabled(!busy);
    if (emulatorRemoveButton_)
        emulatorRemoveButton_->setEnabled(!busy);
    if (emulatorDetectButton_)
        emulatorDetectButton_->setEnabled(!busy && !detectionRunning_);
    if (mappingSaveButton_)
        mappingSaveButton_->setEnabled(!busy && mappingsDirty_);
    if (mappingRevertButton_)
        mappingRevertButton_->setEnabled(!busy && mappingsDirty_);
    if (mappingRefreshButton_)
        mappingRefreshButton_->setEnabled(!busy);
    if (backupAction_)
        backupAction_->setEnabled(!busy);
    if (restoreAction_)
        restoreAction_->setEnabled(!busy);
    if (verifyHashesAction_)
        verifyHashesAction_->setEnabled(!busy);
    if (hashVerifyButton_)
        hashVerifyButton_->setEnabled(!busy);
    refreshImportSelectionState();
    updateLibraryActions();
    updateBulkBar();

    if (statusBusyLabel_) {
        if (importRunning_)
            statusBusyLabel_->setText(QStringLiteral("Importing..."));
        else if (scanRunning_)
            statusBusyLabel_->setText(QStringLiteral("Scanning..."));
        else if (detectionRunning_)
            statusBusyLabel_->setText(QStringLiteral("Detecting emulators..."));
        else if (taskRunning_)
            statusBusyLabel_->setText(taskName_.isEmpty() ? QStringLiteral("Working...")
                                                          : taskName_);
        statusBusyLabel_->setVisible(importRunning_ || scanRunning_ || detectionRunning_
                                     || taskRunning_);
    }
}

void MainWindow::updateHeaderStatus() {
    if (!dataDirectoryEdit_)
        return;
    dataDirectoryEdit_->setText(QDir::toNativeSeparators(settings_.dataDirectory));
    dataDirectoryEdit_->setToolTip(settings_.dataDirectory);
    if (settingsDataDirectoryEdit_)
        settingsDataDirectoryEdit_->setText(QDir::toNativeSeparators(settings_.dataDirectory));

    QString detectorText;
    QColor detectorColor(0x8e, 0x99, 0xa8);
    const EmulatorProfile* rpcs3 = findProfile(profiles_, QStringLiteral("rpcs3"));
    if (rpcs3 && !rpcs3->executable.trimmed().isEmpty()
        && QFileInfo(rpcs3->executable).isFile()) {
        detectorText = QStringLiteral("RPCS3 detected at %1")
                           .arg(QDir::toNativeSeparators(rpcs3->executable));
        detectorColor = QColor(0x8f, 0xd1, 0x8f);
    } else {
        const EmulatorProfile* found = nullptr;
        int installed = 0;
        for (const EmulatorProfile& profile : profiles_) {
            if (profile.executable.trimmed().isEmpty()
                || !QFileInfo(profile.executable).isFile())
                continue;
            ++installed;
            if (!found)
                found = &profile;
        }
        if (found) {
            detectorText = QStringLiteral("%1 detected at %2")
                               .arg(found->name.isEmpty() ? found->id : found->name,
                                    QDir::toNativeSeparators(found->executable));
            if (installed > 1)
                detectorText += QStringLiteral(" (+%1 more)").arg(installed - 1);
            detectorColor = QColor(0x8f, 0xd1, 0x8f);
        } else if (detectionRunning_) {
            detectorText = QStringLiteral("Detecting installed emulators...");
        } else {
            detectorText = QStringLiteral(
                "No emulators detected. Use Emulators > Detect installed, or set an executable "
                "path manually.");
        }
    }
    detectorLabel_->setText(detectorText);
    detectorLabel_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(detectorColor.name()));

    const QString xmbDirectory = xmbDataDirectory();
    const bool xmbExists = QFileInfo(xmbDirectory).isDir();
    if (xmbBanner_) {
        xmbBanner_->setVisible(xmbExists);
        if (xmbExists) {
            const bool sameFolder =
                QDir::cleanPath(settings_.dataDirectory).compare(
                    QDir::cleanPath(xmbDirectory), Qt::CaseInsensitive) == 0;
            if (sameFolder) {
                xmbBannerLabel_->setText(
                    QStringLiteral("The XMB launches from %1, and this is the current data "
                                   "folder.")
                        .arg(QDir::toNativeSeparators(xmbDirectory)));
            } else {
                xmbBannerLabel_->setText(
                    QStringLiteral("The XMB launches from %1. Packages created in the current "
                                   "folder will not appear there until you switch folders.")
                        .arg(QDir::toNativeSeparators(xmbDirectory)));
            }
            useXmbButton_->setVisible(!sameFolder);
        }
    }
}

void MainWindow::ensureLayoutAndRefresh() {
    QString layoutError;
    const bool layoutOk = ensureDataLayout(settings_, layoutError);

    QString profileError;
    const QVector<EmulatorProfile> loaded = loadProfiles(settings_, profileError);
    if (profileError.isEmpty()) {
        profiles_ = loaded;
    } else {
        profiles_ = builtInProfiles();
        startupErrors_.append(QStringLiteral("Emulator profiles: %1").arg(profileError));
    }

    if (!layoutOk)
        setHeaderNotice(QStringLiteral("Data folder: %1").arg(layoutError));
    else if (!startupErrors_.isEmpty())
        setHeaderNotice(startupErrors_.join(QLatin1Char('\n')));

    refreshLibrary();
    refreshPlatformMap();
    refreshDiagnostics();
    populateEmulatorTable();
    reinstallDefaultEmulatorCombos();
    updateImportProfileCombos();
    updateHeaderStatus();
    updateBusyUi();
    if (layoutOk)
        setStatusMessage(QStringLiteral("Ready. Data folder: %1")
                             .arg(QDir::toNativeSeparators(settings_.dataDirectory)));
    runDetection(false);
}

void MainWindow::refreshAll(bool runDetectionNow) {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    QString error;
    const QVector<EmulatorProfile> loaded = loadProfiles(settings_, error);
    if (!error.isEmpty()) {
        setHeaderNotice(QStringLiteral("Emulator profiles: %1").arg(error));
    } else {
        profiles_ = loaded;
        setHeaderNotice(QString());
    }
    refreshLibrary();
    refreshPlatformMap();
    refreshDiagnostics();
    populateEmulatorTable(editingProfileId_);
    reinstallDefaultEmulatorCombos();
    updateImportProfileCombos();
    updateHeaderStatus();
    updateBusyUi();
    if (runDetectionNow && !detectionRunning_)
        runDetection(false);
    setStatusMessage(QStringLiteral("Refreshed: %1 package(s), %2 emulator profile(s), %3 "
                                    "platform mapping(s).")
                         .arg(library_.size())
                         .arg(profiles_.size())
                         .arg(mappings_.size()));
}

bool MainWindow::refreshProfiles() {
    QString error;
    const QVector<EmulatorProfile> loaded = loadProfiles(settings_, error);
    if (!error.isEmpty()) {
        setHeaderNotice(QStringLiteral("Emulator profiles: %1").arg(error));
        return false;
    }
    profiles_ = loaded;
    setHeaderNotice(QString());
    return true;
}

void MainWindow::refreshLibrary() {
    const QStringList keepIds = selectedLibraryIds();
    QString error;
    const QVector<PackageRecord> records = listPackages(settings_, error);
    const bool ok = error.isEmpty();
    if (!ok) {
        libraryMessage_->setText(QStringLiteral("Could not read the library: %1").arg(error));
        libraryMessage_->setVisible(true);
    } else {
        libraryMessage_->setVisible(false);
    }
    library_.clear();
    library_.reserve(records.size());
    for (const PackageRecord& record : records) {
        LibraryRow row;
        row.id = record.id;
        row.title = record.title;
        row.platform = record.platform;
        row.emulatorId = record.emulatorId;
        row.collection = record.collection;
        row.romPath = record.romPath;
        row.romKind = record.romKind;
        row.arguments = record.arguments;
        row.artPath = record.art;
        row.packagePath = record.packagePath;
        row.issue = record.issue;
        row.createdAt = record.createdAt;
        row.packagePresent = record.packagePresent;
        row.registryPresent = record.registryPresent;
        row.orphaned = record.orphaned;
        row.missingPackage = record.missingPackage;
        row.tags = record.tags;
        row.hashLabel = record.hashLabel;
        row.hashValue = record.hashValue;
        row.romMtime = record.romMtime;
        row.recordedSize = record.romSize;
        if (!row.romPath.isEmpty() && record.romKind != QStringLiteral("folder")) {
            const QFileInfo romInfo(row.romPath);
            if (romInfo.exists())
                row.size = romInfo.size();
        }
        library_.append(row);
    }

    refreshCollections();
    rebuildTagFilter();
    rebuildLibraryTable(keepIds);
}

bool MainWindow::libraryRowMatches(const LibraryRow& row) const {
    const QString filter = librarySearch_->text().trimmed();
    if (!filter.isEmpty()) {
        const QString haystack = (row.title + QLatin1Char(' ') + row.platform + QLatin1Char(' ')
                                  + row.collection + QLatin1Char(' ') + row.emulatorId
                                  + QLatin1Char(' ') + row.tags.join(QLatin1Char(' ')))
                                     .toLower();
        if (!haystack.contains(filter.toLower()))
            return false;
    }
    if (!collectionFilter_.isEmpty()) {
        if (collectionFilter_ == noCollectionFilter()) {
            if (!row.collection.trimmed().isEmpty())
                return false;
        } else if (row.collection.compare(collectionFilter_, Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    for (const QString& tag : activeTagFilter_) {
        bool present = false;
        for (const QString& candidate : row.tags) {
            if (candidate.compare(tag, Qt::CaseInsensitive) == 0) {
                present = true;
                break;
            }
        }
        if (!present)
            return false;
    }
    return true;
}

void MainWindow::rebuildLibraryTable(const QStringList& keepIds) {
    updatingLibraryTable_ = true;
    libraryTable_->setRowCount(0);

    QVector<int> visible;
    for (int index = 0; index < library_.size(); ++index) {
        if (libraryRowMatches(library_.at(index)))
            visible.append(index);
    }

    QVector<QPair<QString, QVector<int>>> groups;
    if (groupMode_ == kGroupNone) {
        groups.append({QString(), visible});
    } else {
        QHash<QString, QVector<int>> buckets;
        for (int index : visible) {
            const LibraryRow& row = library_.at(index);
            QString key;
            if (groupMode_ == kGroupCollection)
                key = row.collection;
            else if (groupMode_ == kGroupPlatform)
                key = row.platform;
            else if (groupMode_ == kGroupEmulator)
                key = row.emulatorId.isEmpty() ? QString()
                                               : displayEmulator(profiles_, row.emulatorId);
            if (key.trimmed().isEmpty())
                key = QStringLiteral("(none)");
            buckets[key].append(index);
        }
        QStringList names = buckets.keys();
        names.sort(Qt::CaseInsensitive);
        for (const QString& name : names) {
            QVector<int> members = buckets.value(name);
            std::sort(members.begin(), members.end(), [this](int left, int right) {
                return QString::compare(library_.at(left).title, library_.at(right).title,
                                        Qt::CaseInsensitive) < 0;
            });
            groups.append({name, members});
        }
    }

    const QFont groupFont = [this] {
        QFont font = libraryTable_->font();
        font.setBold(true);
        return font;
    }();
    for (const auto& group : groups) {
        if (groupMode_ != kGroupNone) {
            const int headerRow = libraryTable_->rowCount();
            libraryTable_->insertRow(headerRow);
            QString prefix;
            if (groupMode_ == kGroupCollection)
                prefix = QStringLiteral("Collection");
            else if (groupMode_ == kGroupPlatform)
                prefix = QStringLiteral("Platform");
            else
                prefix = QStringLiteral("Emulator");
            auto* header = new QTableWidgetItem(
                QStringLiteral("%1: %2  (%3)").arg(prefix, group.first).arg(group.second.size()));
            header->setFont(groupFont);
            header->setForeground(QColor(QStringLiteral("#cfe0f2")));
            header->setBackground(QColor(QStringLiteral("#202630")));
            header->setFlags(Qt::ItemIsEnabled);
            header->setToolTip(
                QStringLiteral("Group header inserted by the group-by view; select a data row to "
                               "act on a package."));
            libraryTable_->setItem(headerRow, 0, header);
            libraryTable_->setSpan(headerRow, 0, 1, libraryTable_->columnCount());
            libraryTable_->setRowHeight(headerRow, 26);
        }
        for (int index : group.second) {
            const LibraryRow& row = library_.at(index);
            const int tableRow = libraryTable_->rowCount();
            libraryTable_->insertRow(tableRow);
            auto* titleItem = new QTableWidgetItem(row.title);
            titleItem->setData(Qt::UserRole, index);
            titleItem->setToolTip(QStringLiteral("%1\n%2").arg(row.id, row.romPath));
            auto* platformItem = new QTableWidgetItem(row.platform);
            auto* emulatorItem = new QTableWidgetItem(displayEmulator(profiles_, row.emulatorId));
            auto* collectionItem = new QTableWidgetItem(
                row.collection.isEmpty() ? QStringLiteral("-") : row.collection);
            auto* tagsItem = new QTableWidgetItem(displayTags(row.tags));
            tagsItem->setToolTip(row.tags.join(QStringLiteral(", ")));
            auto* sizeItem = new QTableWidgetItem(formatBytes(row.size));
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            const QString state = libraryStateText(row);
            auto* stateItem = new QTableWidgetItem(state);
            stateItem->setForeground(QColor(stateColorName(state)));
            if (!row.issue.isEmpty())
                stateItem->setToolTip(row.issue);
            libraryTable_->setItem(tableRow, 0, titleItem);
            libraryTable_->setItem(tableRow, 1, platformItem);
            libraryTable_->setItem(tableRow, 2, emulatorItem);
            libraryTable_->setItem(tableRow, 3, collectionItem);
            libraryTable_->setItem(tableRow, 4, tagsItem);
            libraryTable_->setItem(tableRow, 5, sizeItem);
            libraryTable_->setItem(tableRow, 6, stateItem);
        }
    }
    updatingLibraryTable_ = false;

    if (library_.isEmpty()) {
        libraryStack_->setCurrentWidget(libraryEmptyPage_);
        libraryCountLabel_->setText(QStringLiteral("0 packages"));
    } else {
        libraryStack_->setCurrentWidget(libraryTablePage_);
        QStringList parts;
        parts.append(visible.size() == library_.size()
                         ? QStringLiteral("%1 package(s)").arg(library_.size())
                         : QStringLiteral("%1 of %2 package(s)")
                               .arg(visible.size())
                               .arg(library_.size()));
        if (groupMode_ != kGroupNone)
            parts.append(QStringLiteral("%1 group(s)").arg(groups.size()));
        if (!activeTagFilter_.isEmpty())
            parts.append(QStringLiteral("tags: %1").arg(activeTagFilter_.join(QStringLiteral(" + "))));
        libraryCountLabel_->setText(parts.join(QStringLiteral("; ")));
    }

    QSet<QString> wanted;
    for (const QString& id : keepIds)
        wanted.insert(id);
    QVector<int> rowsToSelect;
    for (int tableRow = 0; tableRow < libraryTable_->rowCount(); ++tableRow) {
        int index = -1;
        if (!libraryIndexForTableRow(tableRow, index))
            continue;
        if (wanted.contains(library_.at(index).id))
            rowsToSelect.append(tableRow);
    }
    if (rowsToSelect.isEmpty()) {
        for (int tableRow = 0; tableRow < libraryTable_->rowCount(); ++tableRow) {
            int index = -1;
            if (libraryIndexForTableRow(tableRow, index)) {
                rowsToSelect.append(tableRow);
                break;
            }
        }
    }
    if (!rowsToSelect.isEmpty() && libraryTable_->selectionModel()) {
        QItemSelectionModel* selection = libraryTable_->selectionModel();
        // Set the current cell FIRST: QTableWidget::setCurrentCell collapses a multi-row
        // selection, so selecting afterwards is what keeps the bulk selection intact.
        libraryTable_->setCurrentCell(rowsToSelect.first(), 0);
        for (int tableRow : rowsToSelect) {
            selection->select(libraryTable_->model()->index(tableRow, 0),
                              QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    updateLibrarySelection();
    updateLibraryActions();
    updateBulkBar();
}

void MainWindow::refreshDiagnostics() {
    int problems = 0;
    const QString report = buildDiagnosticsReport(&problems);
    lastProblemCount_ = problems;
    lastDiagnosticsReport_ = report;
    if (diagnosticsReport_)
        diagnosticsReport_->setPlainText(report);
    if (reportPathEdit_)
        reportPathEdit_->setText(QDir::toNativeSeparators(reportPath()));
    if (diagnosticsMessage_) {
        diagnosticsMessage_->setText(
            problems == 0
                ? QStringLiteral("No problems found.")
                : QStringLiteral("%1 problem(s) found; see the report.").arg(problems));
    }
    if (layoutTree_) {
        layoutTree_->clear();
        auto* root = new QTreeWidgetItem(layoutTree_);
        root->setText(0, QStringLiteral("Data folder"));
        root->setText(1, QDir::toNativeSeparators(settings_.dataDirectory));
        root->setToolTip(0, settings_.dataDirectory);
        root->setExpanded(true);

        const QString appsPath = registryPath(settings_);
        auto* appsItem = new QTreeWidgetItem(root);
        appsItem->setText(0, QStringLiteral("apps.json"));
        appsItem->setText(1, QFileInfo::exists(appsPath)
                                 ? formatBytes(QFileInfo(appsPath).size())
                                 : QStringLiteral("missing"));
        appsItem->setToolTip(0, appsPath);

        auto* pkgItem = new QTreeWidgetItem(root);
        pkgItem->setText(0, QStringLiteral("pkg/"));
        const QDir pkgDir(packageRoot(settings_));
        const QStringList packageIds =
            pkgDir.exists()
                ? pkgDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)
                : QStringList();
        pkgItem->setText(1, QStringLiteral("%1 folder(s)").arg(packageIds.size()));
        pkgItem->setToolTip(0, packageRoot(settings_));
        for (const QString& id : packageIds) {
            auto* item = new QTreeWidgetItem(pkgItem);
            item->setText(0, id);
            item->setText(1, QFileInfo(QDir(packageRoot(settings_)).filePath(id + "/pkg.json"))
                                      .isFile()
                                  ? QStringLiteral("pkg.json present")
                                  : QStringLiteral("no pkg.json"));
            item->setToolTip(0, QDir(packageRoot(settings_)).filePath(id));
        }
        pkgItem->setExpanded(true);

        const QString emulatorsFile = emulatorsPath(settings_);
        auto* emulatorsItem = new QTreeWidgetItem(root);
        emulatorsItem->setText(0, QStringLiteral("emulators.json"));
        emulatorsItem->setText(
            1, QFileInfo::exists(emulatorsFile) ? formatBytes(QFileInfo(emulatorsFile).size())
                                               : QStringLiteral("missing (built-ins only)"));
        emulatorsItem->setToolTip(0, emulatorsFile);

        const QString collectionsFile = collectionsPath(settings_);
        auto* collectionsItem = new QTreeWidgetItem(root);
        collectionsItem->setText(0, QStringLiteral("collections.json"));
        collectionsItem->setText(
            1, QFileInfo::exists(collectionsFile) ? formatBytes(QFileInfo(collectionsFile).size())
                                                  : QStringLiteral("missing (no collections)"));
        collectionsItem->setToolTip(0, collectionsFile);

        auto* iniItem = new QTreeWidgetItem(root);
        iniItem->setText(0, QStringLiteral("manager.ini"));
        iniItem->setText(1, QFileInfo::exists(settings_.iniPath)
                                 ? QStringLiteral("present")
                                 : QStringLiteral("missing"));
        iniItem->setToolTip(0, settings_.iniPath);

        auto* uiIniItem = new QTreeWidgetItem(root);
        uiIniItem->setText(0, QStringLiteral("manager-ui.ini"));
        const QString uiIni = QDir(settings_.executableDirectory).filePath(
            QStringLiteral("manager-ui.ini"));
        uiIniItem->setText(1, QFileInfo::exists(uiIni) ? QStringLiteral("present")
                                                       : QStringLiteral("missing"));
        uiIniItem->setToolTip(0, uiIni);
        layoutTree_->resizeColumnToContents(0);
    }
}

QStringList MainWindow::mappingPlatforms() const {
    QStringList platforms = knownPlatforms();
    for (const PlatformMapping& mapping : mappings_) {
        if (!platforms.contains(mapping.platform, Qt::CaseInsensitive))
            platforms.append(mapping.platform);
    }
    std::sort(platforms.begin(), platforms.end(), [](const QString& left, const QString& right) {
        return QString::compare(left, right, Qt::CaseInsensitive) < 0;
    });
    return platforms;
}

QString MainWindow::mappingSourceFor(const QString& platform, QString* reason) const {
    Settings settings = settings_;
    QString emulatorId;
    QStringList argumentOverride;
    bool fromMap = false;
    QString localReason;
    const bool loaded =
        resolvePlatformTarget(settings, platform, emulatorId, argumentOverride, fromMap,
                              localReason);
    if (reason)
        *reason = loaded ? (emulatorId.isEmpty() ? localReason : QString()) : localReason;
    if (!loaded)
        return QStringLiteral("error");
    return mappingSourceText(fromMap, !emulatorId.isEmpty());
}

int MainWindow::mappingGapCount() const {
    int gaps = 0;
    for (const QString& platform : mappingPlatforms()) {
        if (mappingSourceFor(platform) == QStringLiteral("unmapped"))
            ++gaps;
    }
    return gaps;
}

QString MainWindow::mappingStatusText() const {
    const QStringList platforms = mappingPlatforms();
    int fromMap = 0;
    int fallback = 0;
    int gaps = 0;
    int errors = 0;
    for (const QString& platform : platforms) {
        const QString source = mappingSourceFor(platform);
        if (source == QStringLiteral("map"))
            ++fromMap;
        else if (source == QStringLiteral("profile fallback"))
            ++fallback;
        else if (source == QStringLiteral("unmapped"))
            ++gaps;
        else
            ++errors;
    }
    QString text = QStringLiteral("%1 platform(s): %2 from the map, %3 profile fallback, %4 "
                                  "unmapped")
                       .arg(platforms.size())
                       .arg(fromMap)
                       .arg(fallback)
                       .arg(gaps);
    if (errors > 0)
        text += QStringLiteral(", %1 unreadable").arg(errors);
    if (mappingsDirty_)
        text += QStringLiteral(" - unsaved edits");
    return text;
}

void MainWindow::updateMappingBanner() {
    if (!mappingBanner_)
        return;
    mappingGapCount_ = mappingGapCount();
    const bool show = mappingGapCount_ > 0;
    mappingBanner_->setVisible(show);
    if (!show)
        return;
    QStringList gaps;
    for (const QString& platform : mappingPlatforms()) {
        if (mappingSourceFor(platform) == QStringLiteral("unmapped"))
            gaps.append(platform);
    }
    QString listed = gaps.mid(0, 8).join(QStringLiteral(", "));
    if (gaps.size() > 8)
        listed += QStringLiteral(", ...");
    mappingBannerLabel_->setText(
        QStringLiteral("%1 of the known platforms have no mapping and no emulator profile "
                       "fallback, so importing them fails with an explicit reason: %2")
            .arg(gaps.size())
            .arg(listed));
}

void MainWindow::updateMappingState() {
    if (mappingSaveButton_)
        mappingSaveButton_->setEnabled(!isBusy() && mappingsDirty_);
    if (mappingRevertButton_)
        mappingRevertButton_->setEnabled(!isBusy() && mappingsDirty_);
    if (mappingFileStateLabel_) {
        QString text = mappingFileState_;
        if (mappingsDirty_)
            text += QStringLiteral(" | unsaved edits (Save map writes platform-map.json)");
        mappingFileStateLabel_->setText(text);
    }
    if (mappingStatus_) {
        mappingStatus_->setText(mappingStatusText());
        mappingStatus_->setStyleSheet(
            mappingError_.isEmpty()
                ? QString()
                : QStringLiteral("QLabel { color: #ef9a9a; }"));
        mappingStatus_->setToolTip(mappingError_);
    }
    updateMappingBanner();
}

void MainWindow::refreshPlatformMap() {
    const QString keep = mappingSelection_;
    QVector<PlatformMapping> mappings;
    QString error;
    if (loadPlatformMap(settings_, mappings, error)) {
        mappings_ = mappings;
        mappingError_.clear();
    } else {
        mappingError_ = error;
        mappings_ = defaultPlatformMappings();
    }
    mappingFileState_ =
        QFileInfo::exists(platformMapPath(settings_))
            ? QStringLiteral("platform-map.json: present (%1)")
                  .arg(QDir::toNativeSeparators(platformMapPath(settings_)))
            : QStringLiteral("platform-map.json: missing, so the core's seeded defaults are in "
                             "effect (%1)")
                  .arg(QDir::toNativeSeparators(platformMapPath(settings_)));
    mappingsDirty_ = false;
    rebuildPlatformTable(keep);
    updateMappingState();
    if (!mappingError_.isEmpty())
        setHeaderNotice(QStringLiteral("Platform map: %1").arg(mappingError_));
}

bool MainWindow::mappingRowForTable(int tableRow, QString& platform) const {
    platform.clear();
    if (!mappingTable_ || tableRow < 0 || tableRow >= mappingTable_->rowCount())
        return false;
    const QTableWidgetItem* item = mappingTable_->item(tableRow, 0);
    if (!item)
        return false;
    platform = item->data(Qt::UserRole).toString();
    return !platform.isEmpty();
}

void MainWindow::rebuildPlatformTable(const QString& keepPlatform) {
    if (!mappingTable_)
        return;
    mappingLoadingEditor_ = true;
    mappingTable_->setRowCount(0);
    for (const QString& platform : mappingPlatforms()) {
        const int row = mappingTable_->rowCount();
        mappingTable_->insertRow(row);
        auto* platformItem = new QTableWidgetItem(platform);
        platformItem->setData(Qt::UserRole, platform);
        const PlatformMapping* mapping = findPlatformMapping(mappings_, platform);
        const QString emulatorId = mapping ? mapping->emulatorId : QString();
        const QString source = mappingSourceFor(platform);
        auto* combo = new QComboBox(mappingTable_);
        combo->setObjectName(QStringLiteral("mappingCombo"));
        combo->addItem(QStringLiteral("(unmapped)"), QString());
        for (const EmulatorProfile& profile : profiles_) {
            combo->addItem(profile.name.isEmpty() ? profile.id : profile.name, profile.id);
            combo->setItemData(combo->count() - 1,
                               QStringLiteral("Profile id: %1").arg(profile.id),
                               Qt::ToolTipRole);
        }
        int index = combo->findData(emulatorId);
        if (index < 0 && !emulatorId.isEmpty()) {
            // The map names an emulator with no profile; keep it visible as a gap.
            combo->addItem(emulatorId, emulatorId);
            combo->setItemData(combo->count() - 1,
                               QStringLiteral("Mapped id '%1' has no emulator profile")
                                   .arg(emulatorId),
                               Qt::ToolTipRole);
            index = combo->count() - 1;
        }
        if (index < 0)
            index = 0;
        combo->setCurrentIndex(index);
        combo->setToolTip(
            QStringLiteral("An explicit mapping wins even when the profile does not list the "
                           "platform. (unmapped) removes the entry."));
        combo->setProperty("mappingPlatform", platform);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo](int) {
            if (mappingLoadingEditor_)
                return;
            const QString platform = combo->property("mappingPlatform").toString();
            if (platform.isEmpty())
                return;
            const PlatformMapping* entry = findPlatformMapping(mappings_, platform);
            const QStringList arguments = entry ? entry->arguments : QStringList();
            setMappingForPlatform(platform, combo->currentData().toString(), arguments);
        });
        auto* overrideItem = new QTableWidgetItem(
            mapping && !mapping->arguments.isEmpty() ? formatArguments(mapping->arguments)
                                                     : QStringLiteral("(profile template)"));
        overrideItem->setToolTip(
            mapping && !mapping->arguments.isEmpty()
                ? QStringLiteral("Each element is one argv element. Edit it in the editor on the "
                                 "right (one element per line).")
                : QStringLiteral("No override: the selected profile's argument template applies."));
        auto* sourceItem = new QTableWidgetItem(source);
        sourceItem->setForeground(QColor(mappingSourceColor(source)));
        sourceItem->setToolTip(
            QStringLiteral("map: an entry in platform-map.json picks the emulator.\n"
                           "profile fallback: no entry, but a profile lists the platform.\n"
                           "unmapped: neither, so import refuses this platform."));
        mappingTable_->setItem(row, 0, platformItem);
        mappingTable_->setCellWidget(row, 1, combo);
        mappingTable_->setItem(row, 2, overrideItem);
        mappingTable_->setItem(row, 3, sourceItem);
    }
    mappingLoadingEditor_ = false;

    int selectRow = -1;
    for (int row = 0; row < mappingTable_->rowCount(); ++row) {
        QString platform;
        if (mappingRowForTable(row, platform)
            && platform.compare(keepPlatform, Qt::CaseInsensitive) == 0) {
            selectRow = row;
            break;
        }
    }
    if (selectRow < 0 && mappingTable_->rowCount() > 0)
        selectRow = 0;
    if (selectRow >= 0) {
        mappingTable_->selectRow(selectRow);
        QString platform;
        if (mappingRowForTable(selectRow, platform))
            loadMappingEditor(platform);
    } else {
        loadMappingEditor(QString());
    }
}

void MainWindow::loadMappingEditor(const QString& platform) {
    if (!mappingEmulatorCombo_ || !mappingArgumentsEdit_)
        return;
    mappingLoadingEditor_ = true;
    mappingSelection_ = platform;
    mappingEmulatorCombo_->clear();
    mappingEmulatorCombo_->addItem(QStringLiteral("(unmapped)"), QString());
    for (const EmulatorProfile& profile : profiles_) {
        mappingEmulatorCombo_->addItem(profile.name.isEmpty() ? profile.id : profile.name,
                                       profile.id);
        mappingEmulatorCombo_->setItemData(mappingEmulatorCombo_->count() - 1,
                                           QStringLiteral("Profile id: %1").arg(profile.id),
                                           Qt::ToolTipRole);
    }
    if (platform.isEmpty()) {
        mappingEditorTitle_->setText(QStringLiteral("Select a platform"));
        mappingEmulatorCombo_->setEnabled(false);
        mappingArgumentsEdit_->setEnabled(false);
        mappingArgumentsEdit_->clear();
        if (mappingClearOverrideButton_)
            mappingClearOverrideButton_->setEnabled(false);
        mappingEditorHint_->setText(
            QStringLiteral("Pick a platform row to edit its emulator and argument override."));
        mappingLoadingEditor_ = false;
        return;
    }
    mappingEditorTitle_->setText(QStringLiteral("Selected platform: %1").arg(platform));
    mappingEmulatorCombo_->setEnabled(true);
    mappingArgumentsEdit_->setEnabled(true);
    if (mappingClearOverrideButton_)
        mappingClearOverrideButton_->setEnabled(true);
    const PlatformMapping* mapping = findPlatformMapping(mappings_, platform);
    const QString emulatorId = mapping ? mapping->emulatorId : QString();
    int index = mappingEmulatorCombo_->findData(emulatorId);
    if (index < 0 && !emulatorId.isEmpty()) {
        mappingEmulatorCombo_->addItem(emulatorId, emulatorId);
        index = mappingEmulatorCombo_->count() - 1;
    }
    if (index < 0)
        index = 0;
    mappingEmulatorCombo_->setCurrentIndex(index);
    mappingArgumentsEdit_->setPlainText(mapping ? mapping->arguments.join(QLatin1Char('\n'))
                                                : QString());

    Settings settings = settings_;
    QString resolvedId;
    QStringList resolvedOverride;
    bool fromMap = false;
    QString reason;
    const bool loaded = resolvePlatformTarget(settings, platform, resolvedId, resolvedOverride,
                                              fromMap, reason);
    QString hint;
    if (!loaded) {
        hint = QStringLiteral("The platform map or the profiles could not be read: %1").arg(reason);
    } else if (resolvedId.isEmpty()) {
        hint = QStringLiteral("Resolved for import: nothing. %1").arg(reason);
    } else if (resolvedOverride.isEmpty()) {
        hint = QStringLiteral("Resolved for import: %1 (%2); profile argument template")
                   .arg(resolvedId, mappingSourceText(fromMap, true));
    } else {
        hint = QStringLiteral("Resolved for import: %1 (%2); argument override in effect")
                   .arg(resolvedId, mappingSourceText(fromMap, true));
    }
    mappingEditorHint_->setText(hint + QStringLiteral("\nPlaceholders: {rom}, {title}, {id}, "
                                                      "{dir}, {core}. One argv element per line; "
                                                      "no quoting and no shell."));
    mappingLoadingEditor_ = false;
}

void MainWindow::setMappingForPlatform(const QString& platform, const QString& emulatorId,
                                       const QStringList& arguments) {
    if (mappingLoadingEditor_ || platform.trimmed().isEmpty())
        return;
    const bool removing = emulatorId.trimmed().isEmpty();
    bool found = false;
    for (int index = 0; index < mappings_.size(); ++index) {
        PlatformMapping& mapping = mappings_[index];
        if (mapping.platform.compare(platform, Qt::CaseInsensitive) != 0)
            continue;
        found = true;
        if (removing) {
            mappings_.removeAt(index);
        } else {
            mapping.emulatorId = emulatorId;
            mapping.arguments = arguments;
        }
        break;
    }
    if (!found && !removing) {
        PlatformMapping mapping;
        mapping.platform = platform;
        mapping.emulatorId = emulatorId;
        mapping.arguments = arguments;
        mappings_.append(mapping);
    }
    mappingsDirty_ = true;

    const PlatformMapping* entry = findPlatformMapping(mappings_, platform);
    const QStringList shownArguments = entry ? entry->arguments : QStringList();
    const QString source = mappingSourceFor(platform);
    for (int row = 0; row < mappingTable_->rowCount(); ++row) {
        QString rowPlatform;
        if (!mappingRowForTable(row, rowPlatform)
            || rowPlatform.compare(platform, Qt::CaseInsensitive) != 0)
            continue;
        if (auto* combo = qobject_cast<QComboBox*>(mappingTable_->cellWidget(row, 1))) {
            QSignalBlocker blocker(combo);
            int index = combo->findData(entry ? entry->emulatorId : QString());
            if (index < 0)
                index = 0;
            combo->setCurrentIndex(index);
        }
        if (QTableWidgetItem* item = mappingTable_->item(row, 2)) {
            item->setText(shownArguments.isEmpty() ? QStringLiteral("(profile template)")
                                                   : formatArguments(shownArguments));
        }
        if (QTableWidgetItem* item = mappingTable_->item(row, 3)) {
            item->setText(source);
            item->setForeground(QColor(mappingSourceColor(source)));
        }
        break;
    }

    if (platform.compare(mappingSelection_, Qt::CaseInsensitive) == 0) {
        const bool wasLoading = mappingLoadingEditor_;
        mappingLoadingEditor_ = true;
        if (removing) {
            mappingEmulatorCombo_->setCurrentIndex(0);
        } else {
            int index = mappingEmulatorCombo_->findData(emulatorId);
            if (index >= 0)
                mappingEmulatorCombo_->setCurrentIndex(index);
        }
        const QString text = shownArguments.join(QLatin1Char('\n'));
        if (mappingArgumentsEdit_->toPlainText() != text)
            mappingArgumentsEdit_->setPlainText(text);
        mappingLoadingEditor_ = wasLoading;
    }
    updateMappingState();
}

void MainWindow::applyMappingEditor() {
    if (mappingLoadingEditor_ || mappingSelection_.isEmpty())
        return;
    QStringList arguments;
    for (const QString& line : mappingArgumentsEdit_->toPlainText().split(QLatin1Char('\n'))) {
        if (!line.isEmpty())
            arguments.append(line);
    }
    setMappingForPlatform(mappingSelection_, mappingEmulatorCombo_->currentData().toString(),
                          arguments);
}

void MainWindow::setMappingArguments(const QString& platform, const QStringList& arguments) {
    if (mappingLoadingEditor_ || platform.isEmpty())
        return;
    const PlatformMapping* entry = findPlatformMapping(mappings_, platform);
    if (!entry) {
        setStatusMessage(
            QStringLiteral("Map '%1' to an emulator before giving it an argument override.")
                .arg(platform));
        return;
    }
    setMappingForPlatform(platform, entry->emulatorId, arguments);
}

void MainWindow::clearMappingOverride() {
    if (mappingSelection_.isEmpty())
        return;
    setMappingArguments(mappingSelection_, QStringList());
    setStatusMessage(
        QStringLiteral("Argument override cleared for '%1'; the profile template applies.")
            .arg(mappingSelection_));
}

void MainWindow::saveMappingTable() {
    if (isBusy())
        return;
    if (!mappingsDirty_) {
        setStatusMessage(QStringLiteral("No mapping changes to save."));
        return;
    }
    QString error;
    if (!savePlatformMap(settings_, mappings_, error)) {
        if (mappingStatus_) {
            mappingStatus_->setText(QStringLiteral("Save failed: %1").arg(error));
            mappingStatus_->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
        }
        setStatusMessage(QStringLiteral("Platform map save failed: %1").arg(error));
        return;
    }
    const int saved = mappings_.size();
    refreshPlatformMap();
    refreshDiagnostics();
    updateImportProfileCombos();
    setStatusMessage(QStringLiteral("Saved %1 mapping(s) to platform-map.json.").arg(saved));
    if (mappingStatus_)
        mappingStatus_->setStyleSheet(QString());
}

void MainWindow::revertMappingTable() {
    if (isBusy())
        return;
    refreshPlatformMap();
    setStatusMessage(QStringLiteral("Unsaved mapping edits discarded; the file was re-read."));
}

void MainWindow::onMappingSelectionChanged() {
    if (mappingLoadingEditor_ || !mappingTable_)
        return;
    const int row = mappingTable_->currentRow();
    QString platform;
    if (!mappingRowForTable(row, platform))
        return;
    loadMappingEditor(platform);
}

void MainWindow::updateImportProfileCombos() {
    if (!importTable_)
        return;
    QHash<QString, QString> emulatorByPlatform;
    const auto resolvedEmulator = [this, &emulatorByPlatform](const QString& platform) {
        const auto found = emulatorByPlatform.constFind(platform);
        if (found != emulatorByPlatform.constEnd())
            return found.value();
        const QString value = emulatorForPlatform(platform);
        emulatorByPlatform.insert(platform, value);
        return value;
    };
    for (int tableRow = 0; tableRow < importRows_.size(); ++tableRow) {
        auto* combo = qobject_cast<QComboBox*>(importTable_->cellWidget(tableRow, 5));
        if (!combo)
            continue;
        const bool userChosen = importRows_.at(tableRow).emulatorUserChosen;
        const QString previous = importRows_.at(tableRow).emulatorId;
        combo->blockSignals(true);
        combo->clear();
        if (profiles_.isEmpty()) {
            combo->addItem(QStringLiteral("(no profiles)"), QString());
        } else {
            for (const EmulatorProfile& profile : profiles_)
                combo->addItem(profile.name.isEmpty() ? profile.id : profile.name, profile.id);
        }
        int index = userChosen ? combo->findData(previous) : -1;
        if (index < 0)
            index = combo->findData(resolvedEmulator(importRows_.at(tableRow).game.platform));
        if (index < 0)
            index = combo->findData(previous);
        if (index < 0 && combo->count() > 0)
            index = 0;
        combo->setCurrentIndex(index);
        importRows_[tableRow].emulatorId = combo->currentData().toString();
        combo->blockSignals(false);
    }
}

void MainWindow::setHeaderNotice(const QString& message) {
    if (!headerNotice_)
        return;
    headerNotice_->setText(message);
    headerNotice_->setVisible(!message.trimmed().isEmpty());
    headerNotice_->setToolTip(message);
}

void MainWindow::setStatusMessage(const QString& message, int timeout) {
    statusBar()->showMessage(message, timeout);
}

void MainWindow::appendImportReport(const QString& line) {
    if (!importReport_)
        return;
    importReport_->appendPlainText(line);
}

QVariant MainWindow::uiValue(const QString& key, const QVariant& fallback) const {
    if (!uiSettings_)
        return fallback;
    return uiSettings_->value(key, fallback);
}

void MainWindow::setUiValue(const QString& key, const QVariant& value) {
    if (!uiSettings_)
        return;
    uiSettings_->setValue(key, value);
    uiSettings_->sync();
}

QString MainWindow::defaultCollection() const {
    return importCollectionEdit_ ? importCollectionEdit_->text().trimmed() : QString();
}

QString MainWindow::xmbDataDirectory() const {
    return QDir(settings_.executableDirectory).filePath(QStringLiteral("data"));
}

QString MainWindow::lastBrowseDirectory() const {
    if (!lastBrowseDirectory_.isEmpty() && QFileInfo(lastBrowseDirectory_).isDir())
        return lastBrowseDirectory_;
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

void MainWindow::browseDataDirectory() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose the manager data folder"),
        QDir::toNativeSeparators(settings_.dataDirectory), QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Switch data folder"),
        QStringLiteral("Switch the manager to this data folder?\n\n%1\n\nPackages already "
                       "imported stay in the old folder; nothing is moved or deleted.")
            .arg(QDir::toNativeSeparators(directory)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    changeDataDirectory(directory);
}

bool MainWindow::changeDataDirectory(const QString& directory) {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return false;
    }
    Settings next = settings_;
    QString error;
    if (!setDataDirectory(next, directory, error, false)) {
        setHeaderNotice(QStringLiteral("Data folder: %1").arg(error));
        if (settingsMessage_) {
            settingsMessage_->setText(error);
            settingsMessage_->setVisible(true);
        }
        return false;
    }
    if (!ensureDataLayout(next, error)) {
        setHeaderNotice(QStringLiteral("Data folder: %1").arg(error));
        if (settingsMessage_) {
            settingsMessage_->setText(error);
            settingsMessage_->setVisible(true);
        }
        return false;
    }
    if (!saveSettings(next, error)) {
        setHeaderNotice(QStringLiteral("Data folder setting: %1").arg(error));
        return false;
    }
    settings_ = next;
    // Verification results belong to the previous data folder's ROM paths.
    lastHashChecks_.clear();
    lastHashCheckTime_.clear();
    if (settingsMessage_)
        settingsMessage_->setVisible(false);
    setHeaderNotice(QString());
    refreshProfiles();
    refreshLibrary();
    refreshPlatformMap();
    refreshDiagnostics();
    populateEmulatorTable(editingProfileId_);
    reinstallDefaultEmulatorCombos();
    updateImportProfileCombos();
    updateHeaderStatus();
    updateBusyUi();
    setStatusMessage(QStringLiteral("Data folder changed to %1")
                         .arg(QDir::toNativeSeparators(settings_.dataDirectory)));
    return true;
}

void MainWindow::openDataFolder() {
    QString error;
    if (!ensureDataLayout(settings_, error)) {
        setHeaderNotice(QStringLiteral("Data folder: %1").arg(error));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(settings_.dataDirectory)))
        setStatusMessage(QStringLiteral("Could not open the data folder."));
}

void MainWindow::useDetectedXmbDataDirectory() {
    const QString directory = xmbDataDirectory();
    if (!QFileInfo(directory).isDir())
        return;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Use the XMB data folder"),
        QStringLiteral("Switch the manager to the folder the XMB launches from?\n\n%1\n\n"
                       "Packages stay where they are; nothing is moved or deleted.")
            .arg(QDir::toNativeSeparators(directory)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes)
        changeDataDirectory(directory);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (isBusy()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("Quit while busy"),
            QStringLiteral("A scan or import is still running. Quit anyway?\n\nAny in-progress "
                           "package write is rolled back by the core; completed packages stay."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        importCancel_.store(true);
    }
    saveUiState();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (localPathsFromMime(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QStringList paths = localPathsFromMime(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    addScannedPaths(paths);
    event->acceptProposedAction();
}

QString MainWindow::libraryStateText(const LibraryRow& row) const {
    if (row.missingPackage)
        return QStringLiteral("registry-only");
    if (row.orphaned)
        return QStringLiteral("orphan");
    if (!row.issue.trimmed().isEmpty())
        return QStringLiteral("invalid");
    if (row.packagePresent && row.registryPresent)
        return QStringLiteral("ok");
    return QStringLiteral("invalid");
}

int MainWindow::selectedLibraryIndex() const {
    if (!libraryTable_)
        return -1;
    int index = -1;
    const int current = libraryTable_->currentRow();
    if (current >= 0) {
        const QTableWidgetItem* currentItem = libraryTable_->item(current, 0);
        if (currentItem && currentItem->isSelected() && libraryIndexForTableRow(current, index))
            return index;
    }
    if (QItemSelectionModel* selection = libraryTable_->selectionModel()) {
        for (const QModelIndex& modelIndex : selection->selectedRows()) {
            if (libraryIndexForTableRow(modelIndex.row(), index))
                return index;
        }
    }
    return -1;
}

QVector<int> MainWindow::selectedLibraryIndices() const {
    QVector<int> indices;
    if (!libraryTable_ || !libraryTable_->selectionModel())
        return indices;
    QModelIndexList rows = libraryTable_->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& left, const QModelIndex& right) {
        return left.row() < right.row();
    });
    for (const QModelIndex& modelIndex : rows) {
        int index = -1;
        if (!libraryIndexForTableRow(modelIndex.row(), index))
            continue;
        if (!indices.contains(index))
            indices.append(index);
    }
    return indices;
}

QStringList MainWindow::selectedLibraryIds() const {
    QStringList ids;
    const QVector<int> indices = selectedLibraryIndices();
    for (int index : indices)
        ids.append(library_.at(index).id);
    return ids;
}

bool MainWindow::libraryIndexForTableRow(int tableRow, int& index) const {
    index = -1;
    if (!libraryTable_ || tableRow < 0 || tableRow >= libraryTable_->rowCount())
        return false;
    const QTableWidgetItem* item = libraryTable_->item(tableRow, 0);
    if (!item)
        return false;
    const QVariant value = item->data(Qt::UserRole);
    if (!value.isValid())
        return false;
    index = value.toInt();
    return index >= 0 && index < library_.size();
}

void MainWindow::applyLibraryFilter() {
    rebuildLibraryTable(selectedLibraryIds());
}

void MainWindow::updateLibrarySelection() {
    if (!libraryTable_)
        return;
    const int index = selectedLibraryIndex();
    const int selectedCount = selectedLibraryIndices().size();
    if (index < 0 || index >= library_.size()) {
        detailTitle_->setText(QStringLiteral("No package selected"));
        detailState_->clear();
        detailPlatform_->setText(QStringLiteral("-"));
        detailEmulator_->setText(QStringLiteral("-"));
        detailRom_->setText(QStringLiteral("-"));
        detailCollection_->setText(QStringLiteral("-"));
        detailTags_->setText(QStringLiteral("-"));
        detailSize_->setText(QStringLiteral("-"));
        detailHash_->setText(QStringLiteral("-"));
        detailHash_->setToolTip(QString());
        detailHashState_->setText(QStringLiteral("Hash not verified this session"));
        detailArguments_->setText(QStringLiteral("-"));
        detailPackage_->setText(QStringLiteral("-"));
        detailRom_->setToolTip(QString());
        detailPackage_->setToolTip(QString());
        detailArgv_->clear();
        artworkLabel_->setPixmap(QPixmap());
        artworkLabel_->setText(QStringLiteral("No artwork"));
        launchResult_->setText(
            QStringLiteral("Select a package to test its launch or edit its metadata."));
        launchResult_->setStyleSheet(QString());
        updateLibraryActions();
        updateBulkBar();
        return;
    }

    const LibraryRow& row = library_.at(index);
    detailTitle_->setText(row.title.isEmpty() ? row.id : row.title);
    const QString state = libraryStateText(row);
    QString stateText = QStringLiteral("State: %1%2")
                            .arg(state, row.issue.trimmed().isEmpty()
                                            ? QString()
                                            : QStringLiteral(" - %1").arg(row.issue));
    if (selectedCount > 1)
        stateText += QStringLiteral(" | %1 packages selected").arg(selectedCount);
    detailState_->setText(stateText);
    detailState_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(stateColorName(state)));
    detailPlatform_->setText(row.platform.isEmpty() ? QStringLiteral("-") : row.platform);

    QString emulatorText = displayEmulator(profiles_, row.emulatorId);
    ContentApp registered;
    if (findRegisteredApp(settings_, row.id, registered) && !registered.executable.isEmpty()) {
        emulatorText += QStringLiteral("  [%1]").arg(QDir::toNativeSeparators(registered.executable));
    }
    detailEmulator_->setText(emulatorText);
    detailRom_->setText(row.romPath.isEmpty() ? QStringLiteral("(not recorded)") : row.romPath);
    detailRom_->setToolTip(row.romPath);
    detailCollection_->setText(row.collection.isEmpty() ? QStringLiteral("None") : row.collection);
    detailTags_->setText(displayTags(row.tags));
    detailTags_->setToolTip(row.tags.join(QStringLiteral(", ")));
    detailSize_->setText(row.romKind == QStringLiteral("folder")
                             ? QStringLiteral("Folder game (size not measured)")
                             : formatBytes(row.size));
    detailArguments_->setText(row.arguments.isEmpty() ? QStringLiteral("(no arguments recorded)")
                                                      : formatArguments(row.arguments));
    detailArguments_->setToolTip(formatArguments(row.arguments));
    detailPackage_->setText(row.packagePath.isEmpty() ? QStringLiteral("-") : row.packagePath);
    detailPackage_->setToolTip(row.packagePath);

    updateDetailHash(row);

    QString executable;
    QString workingDirectory;
    QStringList arguments;
    QString argvError;
    if (packageArgv(settings_, row.id, executable, workingDirectory, arguments, argvError)) {
        detailArgv_->setPlainText(argvText(executable, workingDirectory, arguments));
        detailArgv_->setToolTip(QString());
    } else {
        detailArgv_->setPlainText(
            QStringLiteral("(packageArgv failed: %1)").arg(argvError));
        detailArgv_->setToolTip(argvError);
    }

    QPixmap artwork;
    if (!row.artPath.isEmpty() && QFileInfo::exists(row.artPath)) {
        QImageReader reader(row.artPath);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull()) {
            const QSize target(300, 168);
            artwork = QPixmap::fromImage(image).scaled(target, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation);
        }
    }
    if (artwork.isNull()) {
        artworkLabel_->setPixmap(QPixmap());
        artworkLabel_->setText(QStringLiteral("No artwork"));
    } else {
        artworkLabel_->setText(QString());
        artworkLabel_->setPixmap(artwork);
    }

    launchResult_->setStyleSheet(QString());
    launchResult_->setText(
        QStringLiteral("Test launch calls the core's launchPackage with the timeout above."));
    updateLibraryActions();
    updateBulkBar();
}

void MainWindow::updateDetailHash(const LibraryRow& row) {
    if (!detailHash_)
        return;
    if (row.hashValue.isEmpty() || row.hashLabel.isEmpty()) {
        detailHash_->setText(QStringLiteral("No hash recorded"));
        detailHash_->setToolTip(
            QStringLiteral("pkg.json has no content hash. verifyPackageHashes reports "
                           "missing-hash; relink with --force records one."));
    } else {
        detailHash_->setText(QStringLiteral("%1 | %2").arg(row.hashLabel, row.hashValue));
        detailHash_->setToolTip(QStringLiteral("%1\n%2\nsize: %3 bytes\nmtime: %4")
                                    .arg(row.hashLabel, row.hashValue)
                                    .arg(row.recordedSize)
                                    .arg(row.romMtime));
    }
    QString stateText = QStringLiteral("Not verified this session");
    QString color = QStringLiteral("#8e99a8");
    for (const HashVerification& check : lastHashChecks_) {
        if (check.id != row.id)
            continue;
        stateText = QStringLiteral("Last verification: %1").arg(check.state);
        if (check.state == QStringLiteral("ok")) {
            color = QStringLiteral("#8fd18f");
        } else if (check.state == QStringLiteral("mismatch")) {
            color = QStringLiteral("#e8bd6a");
        } else if (check.state == QStringLiteral("missing-rom")) {
            color = QStringLiteral("#e8bd6a");
        } else {
            color = QStringLiteral("#ef9a9a");
        }
        if (!check.error.isEmpty())
            stateText += QStringLiteral(" - %1").arg(check.error);
        if (!check.currentHash.isEmpty() && check.state == QStringLiteral("mismatch")) {
            stateText += QStringLiteral(" | current: %1").arg(shortHash(check.currentHash));
        }
        break;
    }
    detailHashState_->setText(stateText);
    detailHashState_->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(color));
}

void MainWindow::updateLibraryActions() {
    const int index = selectedLibraryIndex();
    const bool has = index >= 0 && index < library_.size();
    const bool ready = !isBusy();
    const bool hasRom = has && !library_.at(index).romPath.trimmed().isEmpty();
    if (launchButton_)
        launchButton_->setEnabled(has && ready);
    if (argvButton_)
        argvButton_->setEnabled(has && ready);
    if (copyArgvButton_)
        copyArgvButton_->setEnabled(has && ready);
    if (verifyButton_)
        verifyButton_->setEnabled(has && ready);
    if (editButton_)
        editButton_->setEnabled(has && ready && hasRom);
    if (editTagsButton_)
        editTagsButton_->setEnabled(has && ready && library_.at(index).packagePresent);
    if (relinkButton_)
        relinkButton_->setEnabled(has && ready && hasRom);
    if (removeButton_)
        removeButton_->setEnabled(has && ready);
    if (reimportButton_)
        reimportButton_->setEnabled(has && ready && hasRom);
    if (has) {
        editButton_->setToolTip(
            hasRom ? QStringLiteral("Edit title, collection, emulator, and extra arguments "
                                    "(Ctrl+E)")
                   : QStringLiteral("This registry-only entry has no ROM path to rebuild from"));
        reimportButton_->setToolTip(
            hasRom ? QStringLiteral("Rebuild this package from its ROM")
                   : QStringLiteral("This registry-only entry has no ROM path to rebuild from"));
        relinkButton_->setToolTip(
            hasRom ? QStringLiteral("Point this package at a moved ROM through relinkPackage")
                   : QStringLiteral("This registry-only entry has no ROM path to relink"));
    }
}

void MainWindow::updateBulkBar() {
    if (!bulkBar_)
        return;
    const int count = selectedLibraryIndices().size();
    const bool show = count >= 2;
    bulkBar_->setVisible(show);
    if (!show)
        return;
    bulkLabel_->setText(QStringLiteral("%1 packages selected").arg(count));
    const bool ready = !isBusy();
    for (QPushButton* button :
         {bulkCollectionButton_, bulkEmulatorButton_, bulkAddTagButton_, bulkRemoveTagButton_,
          bulkRemoveButton_, bulkClearButton_}) {
        if (button)
            button->setEnabled(ready);
    }
}

void MainWindow::clearLibraryFilters() {
    librarySearch_->clear();
    activeTagFilter_.clear();
    collectionFilter_.clear();
    rebuildTagFilter();
    rebuildCollectionsList();
    applyLibraryFilter();
    setStatusMessage(QStringLiteral("Library filters cleared."));
}

QStringList MainWindow::extraArgumentsFor(const LibraryRow& row, bool* matchesBase) const {
    if (matchesBase)
        *matchesBase = false;
    const EmulatorProfile* profile = findProfile(profiles_, row.emulatorId);
    if (!profile || row.romPath.isEmpty())
        return QStringList();
    QString error;
    const QStringList values =
        argumentKeyValues(*profile, row.romPath, row.title, row.id, row.platform);
    const QStringList base = expandArguments(*profile, values, error);
    if (!error.isEmpty() || row.arguments.size() < base.size())
        return QStringList();
    for (int index = 0; index < base.size(); ++index) {
        if (row.arguments.at(index) != base.at(index))
            return QStringList();
    }
    if (matchesBase)
        *matchesBase = true;
    return row.arguments.mid(base.size());
}

void MainWindow::launchLibraryEntry() {
    const int index = selectedLibraryIndex();
    if (index < 0 || index >= library_.size()) {
        launchResult_->setText(QStringLiteral("Select a package first."));
        launchResult_->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
        return;
    }
    startLaunch(library_.at(index).id);
}

void MainWindow::startLaunch(const QString& appId) {
    if (!beginTask(QStringLiteral("Testing launch"))) {
        launchResult_->setText(QStringLiteral("Wait for the current task to finish."));
        launchResult_->setStyleSheet(QStringLiteral("QLabel { color: #e8bd6a; }"));
        return;
    }
    const int timeout = launchTimeoutSpin_ ? launchTimeoutSpin_->value() : 8;
    launchResult_->setStyleSheet(QStringLiteral("QLabel { color: #a9bed6; }"));
    launchResult_->setText(
        QStringLiteral("launchPackage is starting the recorded executable (timeout %1 s)...")
            .arg(timeout));
    const Settings settings = settings_;
    taskThread_ = QThread::create([this, settings, appId, timeout] {
        Settings workerSettings = settings;
        LaunchOutcome outcome;
        QString error;
        const bool spawned = launchPackage(workerSettings, appId, timeout, outcome, error);
        QMetaObject::invokeMethod(
            this,
            [this, appId, spawned, outcome, error] { finishLaunch(appId, spawned, outcome, error); },
            Qt::QueuedConnection);
    });
    connect(taskThread_.data(), &QThread::finished, taskThread_.data(), &QObject::deleteLater);
    taskThread_->start();
}

void MainWindow::finishLaunch(const QString& appId, bool spawned, const LaunchOutcome& outcome,
                              const QString& error) {
    endTask();
    const QString summary = outcomeSummary(outcome);
    QString message;
    if (!spawned && !outcome.spawnError.isEmpty()) {
        message = QStringLiteral("Spawn error: %1").arg(outcome.spawnError);
    } else if (!spawned) {
        message = error.isEmpty() ? QStringLiteral("Launch failed.") : error;
    } else if (outcome.timedOut) {
        message = QStringLiteral("Timed out after %1 s (%2) - pid %3")
                      .arg(outcome.timeoutSeconds)
                      .arg(outcome.killed ? QStringLiteral("terminated, then killed")
                                          : QStringLiteral("terminated"))
                      .arg(outcome.pid);
    } else if (outcome.exitStatus == QStringLiteral("crash")) {
        message = QStringLiteral("Process crashed (pid %1; no exit code)").arg(outcome.pid);
    } else {
        message = QStringLiteral("Exited with code %1 (pid %2)")
                      .arg(outcome.exitCode)
                      .arg(outcome.pid);
    }
    launchResult_->setText(message);
    launchResult_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(outcomeColor(outcome)));
    launchResult_->setToolTip(summary);
    setStatusMessage(QStringLiteral("Launch %1: %2")
                         .arg(appId, message));
    if (!spawned || outcome.timedOut || outcome.exitStatus == QStringLiteral("crash")
        || outcome.exitCode != 0) {
        QMessageBox box(this);
        box.setIcon(outcome.timedOut ? QMessageBox::Warning : QMessageBox::Critical);
        box.setWindowTitle(spawned ? QStringLiteral("Launch check failed")
                                   : QStringLiteral("Launch could not start"));
        box.setText(message);
        box.setInformativeText(
            QStringLiteral("The outcome comes from the core's launchPackage for '%1'.").arg(appId));
        box.setDetailedText(summary);
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    }
}

void MainWindow::showArgvForSelected() {
    const int index = selectedLibraryIndex();
    if (index < 0 || index >= library_.size())
        return;
    const LibraryRow& row = library_.at(index);
    QString executable;
    QString workingDirectory;
    QStringList arguments;
    QString error;
    if (!packageArgv(settings_, row.id, executable, workingDirectory, arguments, error)) {
        detailArgv_->setPlainText(QStringLiteral("(packageArgv failed: %1)").arg(error));
        detailArgv_->setToolTip(error);
        setStatusMessage(QStringLiteral("Show argv failed: %1").arg(error));
        return;
    }
    detailArgv_->setPlainText(argvText(executable, workingDirectory, arguments));
    detailArgv_->setToolTip(QString());
    detailArgv_->setFocus();
    setStatusMessage(
        QStringLiteral("argv for %1: %2 element(s) (print-argv semantics, nothing launched).")
            .arg(row.id)
            .arg(arguments.size()));
}

void MainWindow::copyArgv() {
    if (!detailArgv_)
        return;
    const QString text = detailArgv_->toPlainText();
    if (text.trimmed().isEmpty()) {
        setStatusMessage(QStringLiteral("There is no argv to copy."));
        return;
    }
    QApplication::clipboard()->setText(text);
    setStatusMessage(QStringLiteral("argv copied to the clipboard."));
}

void MainWindow::editLibraryEntry() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const int index = selectedLibraryIndex();
    if (index < 0 || index >= library_.size())
        return;
    const LibraryRow row = library_.at(index);
    if (row.romPath.trimmed().isEmpty()) {
        libraryMessage_->setText(
            QStringLiteral("This registry-only entry has no ROM path, so it cannot be rebuilt. "
                           "Remove it and import the ROM again."));
        libraryMessage_->setVisible(true);
        return;
    }
    bool matchesBase = false;
    const QStringList extra = extraArgumentsFor(row, &matchesBase);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit package"));
    dialog.setMinimumWidth(580);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* titleEdit = new QLineEdit(row.title, &dialog);
    auto* collectionEdit = new QLineEdit(row.collection, &dialog);
    collectionEdit->setPlaceholderText(QStringLiteral("Optional collection"));
    auto* emulatorCombo = new QComboBox(&dialog);
    if (profiles_.isEmpty()) {
        emulatorCombo->addItem(QStringLiteral("(no emulator profiles)"), QString());
    } else {
        for (const EmulatorProfile& profile : profiles_)
            emulatorCombo->addItem(profile.name.isEmpty() ? profile.id : profile.name, profile.id);
    }
    int emulatorIndex = emulatorCombo->findData(row.emulatorId);
    if (emulatorIndex < 0 && emulatorCombo->count() > 0)
        emulatorIndex = 0;
    emulatorCombo->setCurrentIndex(emulatorIndex);
    auto* extraEdit = new QPlainTextEdit(&dialog);
    extraEdit->setPlainText(extra.join(QLatin1Char('\n')));
    extraEdit->setPlaceholderText(
        QStringLiteral("One argument per line. Each line becomes one argv element."));
    extraEdit->setMaximumHeight(90);
    auto* tagsEdit = new QLineEdit(row.tags.join(QStringLiteral(", ")), &dialog);
    tagsEdit->setPlaceholderText(QStringLiteral("Comma-separated, e.g. Classic, Co-op"));
    tagsEdit->setToolTip(
        QStringLiteral("Written through updatePackageTags: trimmed, at most %1 characters each, "
                       "de-duplicated case-insensitively.")
            .arg(kMaxTagLength));
    form->addRow(QStringLiteral("Title"), titleEdit);
    form->addRow(QStringLiteral("Collection"), collectionEdit);
    form->addRow(QStringLiteral("Emulator"), emulatorCombo);
    form->addRow(QStringLiteral("Tags"), tagsEdit);
    form->addRow(QStringLiteral("Extra arguments"), extraEdit);
    layout->addLayout(form);

    auto* argumentsCaption = makeMutedLabel(
        &dialog, QStringLiteral("Recorded argv: %1").arg(
                     row.arguments.isEmpty() ? QStringLiteral("(none)")
                                             : formatArguments(row.arguments)));
    argumentsCaption->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(argumentsCaption);
    if (!matchesBase) {
        layout->addWidget(makeMutedLabel(
            &dialog,
            QStringLiteral("The recorded argv was not built from the current template, so extra "
                           "arguments could not be separated. Saving rebuilds argv from the "
                           "template plus the lines above.")));
    }
    auto* error = makeErrorLabel(&dialog);
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this] {
        if (titleEdit->text().trimmed().isEmpty()) {
            error->setText(QStringLiteral("Title must not be empty."));
            error->setVisible(true);
            return;
        }
        if (emulatorCombo->currentData().toString().isEmpty()) {
            error->setText(QStringLiteral("Choose an emulator profile."));
            error->setVisible(true);
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString newTitle = titleEdit->text().trimmed();
    const QString newCollection = collectionEdit->text().trimmed();
    const QString newEmulator = emulatorCombo->currentData().toString();
    QStringList newTags;
    for (const QString& part : tagsEdit->text().split(QLatin1Char(','))) {
        const QString tag = part.trimmed();
        if (!tag.isEmpty())
            newTags.append(tag);
    }
    QStringList newExtra;
    for (const QString& line : extraEdit->toPlainText().split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty())
            newExtra.append(line);
    }
    if (newTitle == row.title && newCollection == row.collection
        && newEmulator.compare(row.emulatorId, Qt::CaseInsensitive) == 0 && newExtra == extra
        && newTags == row.tags) {
        setStatusMessage(QStringLiteral("No changes were made."));
        return;
    }
    const EmulatorProfile* profile = findProfile(profiles_, newEmulator);
    if (!profile) {
        libraryMessage_->setText(QStringLiteral("Emulator profile '%1' was not found.")
                                     .arg(newEmulator));
        libraryMessage_->setVisible(true);
        return;
    }

    DetectedGame game;
    game.title = newTitle;
    game.platform = row.platform;
    game.romPath = row.romPath;
    game.romKind = row.romKind;
    game.size = row.size;
    game.artPath = row.artPath;
    game.sourceNote = QStringLiteral("edited in XMB Manager");

    PackageInput input;
    input.game = game;
    input.emulatorId = newEmulator;
    input.extraArgs = newExtra;
    input.collection = newCollection;
    input.replace = true;

    QString errorMessage;
    if (!createPackage(settings_, input, *profile, nullptr, errorMessage)) {
        libraryMessage_->setText(QStringLiteral("Edit failed: %1").arg(errorMessage));
        libraryMessage_->setVisible(true);
        setStatusMessage(QStringLiteral("Edit failed: %1").arg(errorMessage));
        return;
    }

    // createPackage adds the new membership; setPackageCollection is the core
    // call that also clears the package from every other collection, so a moved
    // package never ends up listed twice.
    QString collectionError;
    if (!setPackageCollection(settings_, row.id, newCollection, collectionError)) {
        appendImportReport(
            QStringLiteral("Collection update failed for %1: %2").arg(row.id, collectionError));
        setStatusMessage(QStringLiteral("Collection update failed: %1").arg(collectionError));
    }

    if (newTags != row.tags) {
        QStringList addTags;
        QStringList removeTags;
        for (const QString& tag : newTags) {
            bool present = false;
            for (const QString& current : row.tags) {
                if (current.compare(tag, Qt::CaseInsensitive) == 0) {
                    present = true;
                    break;
                }
            }
            if (!present)
                addTags.append(tag);
        }
        for (const QString& current : row.tags) {
            bool kept = false;
            for (const QString& tag : newTags) {
                if (tag.compare(current, Qt::CaseInsensitive) == 0) {
                    kept = true;
                    break;
                }
            }
            if (!kept)
                removeTags.append(current);
        }
        QStringList outTags;
        QString tagError;
        if (!updatePackageTags(settings_, row.id, addTags, removeTags, outTags, tagError)) {
            libraryMessage_->setText(QStringLiteral("Tags were not updated: %1").arg(tagError));
            libraryMessage_->setVisible(true);
            setStatusMessage(QStringLiteral("Tag update failed: %1").arg(tagError));
        }
    }

    libraryMessage_->setVisible(false);
    refreshLibrary();
    refreshDiagnostics();
    setStatusMessage(QStringLiteral("Updated '%1'.").arg(newTitle));
}

void MainWindow::removeLibraryEntry() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const QVector<int> indices = selectedLibraryIndices();
    if (indices.isEmpty())
        return;
    if (indices.size() > 1) {
        QStringList ids;
        for (int index : indices)
            ids.append(library_.at(index).id);
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("Remove packages"),
            QStringLiteral("Remove %1 packages from the XMB library?\n\nTheir apps.json "
                           "registrations and pkg/<id>/ folders are deleted. The ROM files and the "
                           "emulators are never touched. The core's applyBulk reports one result "
                           "per package and rolls everything back if a write fails.")
                .arg(ids.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        BulkRequest request;
        request.ids = ids;
        request.remove = true;
        runBulk(request, QStringLiteral("Removing %1 package(s)").arg(ids.size()));
        return;
    }
    const LibraryRow row = library_.at(indices.first());
    const QString title = row.title.isEmpty() ? row.id : row.title;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Remove package"),
        QStringLiteral("Remove '%1' from the XMB library?\n\nThis deletes %2 and its apps.json "
                       "registration. The ROM file and the emulator are never touched.")
            .arg(title, QDir::toNativeSeparators(row.packagePath.isEmpty()
                                                     ? packageDirectoryFor(settings_, row.id)
                                                     : row.packagePath)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!removePackage(settings_, row.id, error)) {
        libraryMessage_->setText(QStringLiteral("Remove failed: %1").arg(error));
        libraryMessage_->setVisible(true);
        setStatusMessage(QStringLiteral("Remove failed: %1").arg(error));
        return;
    }
    libraryMessage_->setVisible(false);
    refreshLibrary();
    refreshDiagnostics();
    setStatusMessage(QStringLiteral("Removed '%1'.").arg(title));
}

void MainWindow::editLibraryTags() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const int index = selectedLibraryIndex();
    if (index < 0 || index >= library_.size())
        return;
    const LibraryRow row = library_.at(index);
    if (!row.packagePresent) {
        libraryMessage_->setText(
            QStringLiteral("This registration has no pkg.json, so there is nowhere to store tags."));
        libraryMessage_->setVisible(true);
        return;
    }
    bool accepted = false;
    const QString text = QInputDialog::getText(
        this, QStringLiteral("Edit tags"),
        QStringLiteral("Tags for '%1' (comma-separated; an empty list clears them)")
            .arg(row.title.isEmpty() ? row.id : row.title),
        QLineEdit::Normal, row.tags.join(QStringLiteral(", ")), &accepted);
    if (!accepted)
        return;
    QStringList wanted;
    for (const QString& part : text.split(QLatin1Char(','))) {
        const QString tag = part.trimmed();
        if (!tag.isEmpty() && !wanted.contains(tag, Qt::CaseInsensitive))
            wanted.append(tag);
    }
    if (wanted == row.tags) {
        setStatusMessage(QStringLiteral("Tags unchanged."));
        return;
    }
    QStringList addTags;
    QStringList removeTags;
    for (const QString& tag : wanted) {
        if (!row.tags.contains(tag, Qt::CaseInsensitive))
            addTags.append(tag);
    }
    for (const QString& tag : row.tags) {
        if (!wanted.contains(tag, Qt::CaseInsensitive))
            removeTags.append(tag);
    }
    QStringList outTags;
    QString error;
    if (!updatePackageTags(settings_, row.id, addTags, removeTags, outTags, error)) {
        libraryMessage_->setText(QStringLiteral("Tag update failed: %1").arg(error));
        libraryMessage_->setVisible(true);
        setStatusMessage(QStringLiteral("Tag update failed: %1").arg(error));
        return;
    }
    libraryMessage_->setVisible(false);
    refreshLibrary();
    refreshDiagnostics();
    setStatusMessage(QStringLiteral("Tags for %1: %2")
                         .arg(row.id, outTags.isEmpty() ? QStringLiteral("(none)")
                                                        : outTags.join(QStringLiteral(", "))));
}

void MainWindow::verifySelectedHashes() {
    const int index = selectedLibraryIndex();
    if (index < 0 || index >= library_.size())
        return;
    startHashVerification(library_.at(index).id);
}

void MainWindow::verifyLibraryHashes() {
    startHashVerification(QString());
}

void MainWindow::startHashVerification(const QString& focusId) {
    if (!beginTask(focusId.isEmpty() ? QStringLiteral("Verifying every package hash")
                                     : QStringLiteral("Verifying the package hash")))
        return;
    if (!focusId.isEmpty()) {
        detailHashState_->setText(QStringLiteral("Verifying..."));
        detailHashState_->setStyleSheet(QStringLiteral("QLabel { color: #a9bed6; }"));
    }
    const Settings settings = settings_;
    taskThread_ = QThread::create([this, settings, focusId] {
        Settings workerSettings = settings;
        QVector<HashVerification> checks;
        QString error;
        const bool ok = verifyPackageHashes(workerSettings, checks, error);
        QMetaObject::invokeMethod(
            this, [this, focusId, ok, checks, error] {
                finishHashVerification(focusId, ok, checks, error);
            },
            Qt::QueuedConnection);
    });
    connect(taskThread_.data(), &QThread::finished, taskThread_.data(), &QObject::deleteLater);
    taskThread_->start();
}

void MainWindow::finishHashVerification(const QString& focusId, bool ok,
                                        const QVector<HashVerification>& checks,
                                        const QString& error) {
    endTask();
    if (!ok) {
        const QString message = QStringLiteral("Hash verification failed: %1").arg(error);
        libraryMessage_->setText(message);
        libraryMessage_->setVisible(true);
        setStatusMessage(message);
        if (focusId.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Verify hashes"), message);
        } else {
            detailHashState_->setText(message);
            detailHashState_->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
        }
        return;
    }
    lastHashChecks_ = checks;
    lastHashCheckTime_ =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    refreshDiagnostics();
    const int index = selectedLibraryIndex();
    if (index >= 0 && index < library_.size())
        updateDetailHash(library_.at(index));
    if (!focusId.isEmpty()) {
        const HashVerification* match = nullptr;
        for (const HashVerification& check : checks) {
            if (check.id == focusId) {
                match = &check;
                break;
            }
        }
        if (!match) {
            setStatusMessage(QStringLiteral("No package record for %1.").arg(focusId));
            return;
        }
        QString message = QStringLiteral("%1: %2").arg(focusId, match->state);
        if (!match->error.isEmpty())
            message += QStringLiteral(" - %1").arg(match->error);
        setStatusMessage(message);
        if (match->state != QStringLiteral("ok")) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(QStringLiteral("Hash verification"));
            box.setText(QStringLiteral("Package '%1': %2").arg(focusId, match->state));
            box.setInformativeText(
                match->state == QStringLiteral("mismatch")
                    ? QStringLiteral("The ROM content changed. Use Relink... with the --force "
                                     "checkbox to accept it deliberately.")
                    : QStringLiteral("The package's ROM cannot be verified as recorded."));
            box.setDetailedText(QStringLiteral("stored: %1 %2\ncurrent: %3\n%4")
                                    .arg(match->storedLabel, match->storedHash,
                                         match->currentHash, match->error));
            box.exec();
        }
    } else {
        int passed = 0;
        QStringList failures;
        for (const HashVerification& check : checks) {
            if (check.state == QStringLiteral("ok"))
                ++passed;
            else
                failures.append(QStringLiteral("%1 | %2 | %3")
                                    .arg(check.state, check.id, check.error));
        }
        const QString summary =
            QStringLiteral("Verify hashes: %1 checked, %2 ok, %3 failed.")
                .arg(checks.size())
                .arg(passed)
                .arg(checks.size() - passed);
        setStatusMessage(summary);
        QMessageBox box(this);
        box.setIcon(failures.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Verify hashes"));
        box.setText(summary);
        box.setInformativeText(failures.isEmpty()
                                   ? QStringLiteral("Every recorded hash matches its ROM.")
                                   : QStringLiteral("See the Diagnostics report for the details."));
        box.setDetailedText(failures.join(QLatin1Char('\n')));
        box.exec();
    }
}

void MainWindow::relinkSelectedPackage() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const int index = selectedLibraryIndex();
    if (index < 0 || index >= library_.size())
        return;
    const LibraryRow row = library_.at(index);
    if (row.romPath.trimmed().isEmpty()) {
        libraryMessage_->setText(QStringLiteral("This entry has no ROM path to relink."));
        libraryMessage_->setVisible(true);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Relink ROM"));
    dialog.setMinimumWidth(620);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    layout->addWidget(makeMutedLabel(
        &dialog,
        QStringLiteral("relinkPackage updates the ROM path, recomputes the content hash, and "
                       "re-expands the argv through the platform map when it applies.")));
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* currentEdit = new QLineEdit(row.romPath, &dialog);
    currentEdit->setReadOnly(true);
    auto* pathRow = new QWidget(&dialog);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(6);
    auto* pathEdit = new QLineEdit(QDir::toNativeSeparators(row.romPath), &dialog);
    pathEdit->setObjectName(QStringLiteral("relinkPathEdit"));
    pathLayout->addWidget(pathEdit, 1);
    auto* browseButton = new QPushButton(QStringLiteral("Browse..."), pathRow);
    pathLayout->addWidget(browseButton);
    form->addRow(QStringLiteral("Current ROM"), currentEdit);
    form->addRow(QStringLiteral("New ROM"), pathRow);
    layout->addLayout(form);
    auto* forceCheck = new QCheckBox(
        QStringLiteral("Accept a different content hash (--force)"), &dialog);
    forceCheck->setObjectName(QStringLiteral("relinkForceCheck"));
    forceCheck->setToolTip(
        QStringLiteral("Only needed when the new file's hash differs from the recorded hash; the "
                       "core refuses that case without force."));
    layout->addWidget(forceCheck);
    layout->addWidget(makeMutedLabel(
        &dialog,
        QStringLiteral("The core refuses a path that is already imported as another package, and "
                       "a folder game can only be relinked to another folder.")));
    auto* error = makeErrorLabel(&dialog);
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, &dialog, [&] {
        const bool folder = row.romKind.compare(QStringLiteral("folder"), Qt::CaseInsensitive) == 0;
        const QString chosen = folder
            ? QFileDialog::getExistingDirectory(
                  &dialog, QStringLiteral("Choose the new ROM folder"),
                  QFileInfo(row.romPath).absolutePath(), QFileDialog::ShowDirsOnly)
            : QFileDialog::getOpenFileName(&dialog, QStringLiteral("Choose the new ROM"),
                                           QFileInfo(row.romPath).absolutePath(), romFilter());
        if (!chosen.isEmpty())
            pathEdit->setText(QDir::toNativeSeparators(chosen));
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (pathEdit->text().trimmed().isEmpty()) {
            error->setText(QStringLiteral("Enter the new ROM path."));
            error->setVisible(true);
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString newPath = QDir::fromNativeSeparators(pathEdit->text().trimmed());
    const bool force = forceCheck->isChecked();
    if (!beginTask(QStringLiteral("Relinking the package")))
        return;
    const Settings settings = settings_;
    const QString appId = row.id;
    taskThread_ = QThread::create([this, settings, appId, newPath, force] {
        Settings workerSettings = settings;
        QJsonObject summary;
        QString error;
        const bool ok = relinkPackage(workerSettings, appId, newPath, force, summary, error);
        QMetaObject::invokeMethod(
            this,
            [this, appId, newPath, force, ok, summary, error] {
                endTask();
                if (!ok) {
                    libraryMessage_->setText(QStringLiteral("Relink failed: %1").arg(error));
                    libraryMessage_->setVisible(true);
                    setStatusMessage(QStringLiteral("Relink failed: %1").arg(error));
                    const bool hashMismatch =
                        error.contains(QStringLiteral("does not match the recorded hash"));
                    if (hashMismatch && !force) {
                        const QMessageBox::StandardButton answer = QMessageBox::question(
                            this, QStringLiteral("Relink with --force"),
                            QStringLiteral("%1\n\nRelink anyway and record the new hash? This is "
                                           "the core's --force path.")
                                .arg(error),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (answer == QMessageBox::Yes) {
                            const Settings retrySettings = settings_;
                            if (!beginTask(QStringLiteral("Relinking the package with --force")))
                                return;
                            taskThread_ = QThread::create([this, retrySettings, appId, newPath] {
                                Settings workerSettings = retrySettings;
                                QJsonObject summary;
                                QString retryError;
                                const bool retried =
                                    relinkPackage(workerSettings, appId, newPath, true, summary,
                                                  retryError);
                                QMetaObject::invokeMethod(
                                    this,
                                    [this, appId, retried, summary, retryError] {
                                        endTask();
                                        if (!retried) {
                                            libraryMessage_->setText(
                                                QStringLiteral("Forced relink failed: %1")
                                                    .arg(retryError));
                                            libraryMessage_->setVisible(true);
                                            setStatusMessage(
                                                QStringLiteral("Forced relink failed: %1")
                                                    .arg(retryError));
                                            return;
                                        }
                                        libraryMessage_->setVisible(false);
                                        refreshLibrary();
                                        refreshDiagnostics();
                                        setStatusMessage(
                                            QStringLiteral("Relinked %1 (forced) to %2")
                                                .arg(appId,
                                                     QDir::toNativeSeparators(
                                                         summary.value(QStringLiteral("newPath"))
                                                             .toString())));
                                    },
                                    Qt::QueuedConnection);
                            });
                            connect(taskThread_.data(), &QThread::finished, taskThread_.data(),
                                    &QObject::deleteLater);
                            taskThread_->start();
                        }
                    }
                    return;
                }
                libraryMessage_->setVisible(false);
                refreshLibrary();
                refreshDiagnostics();
                setStatusMessage(
                    QStringLiteral("Relinked %1 to %2 (%3 %4)")
                        .arg(appId,
                             QDir::toNativeSeparators(
                                 summary.value(QStringLiteral("newPath")).toString()),
                             summary.value(QStringLiteral("hashLabel")).toString(),
                             shortHash(summary.value(QStringLiteral("newHash")).toString())));
            },
            Qt::QueuedConnection);
    });
    connect(taskThread_.data(), &QThread::finished, taskThread_.data(), &QObject::deleteLater);
    taskThread_->start();
}

void MainWindow::reimportLibraryEntry() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const int index = selectedLibraryIndex();
    if (index < 0 || index >= library_.size())
        return;
    const LibraryRow row = library_.at(index);
    if (row.romPath.trimmed().isEmpty()) {
        libraryMessage_->setText(
            QStringLiteral("This registry-only entry has no ROM path to re-import from."));
        libraryMessage_->setVisible(true);
        return;
    }
    const EmulatorProfile* profile = findProfile(profiles_, row.emulatorId);
    if (!profile) {
        libraryMessage_->setText(
            QStringLiteral("The emulator profile '%1' was not found. Choose a profile in Edit "
                           "first.")
                .arg(row.emulatorId));
        libraryMessage_->setVisible(true);
        return;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Re-import package"),
        QStringLiteral("Rebuild '%1' from its ROM?\n\npkg.json, ICON0.PNG, PARAM.SFO, and the "
                       "apps.json entry are rewritten; the ROM is not modified.")
            .arg(row.title.isEmpty() ? row.id : row.title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    bool matchesBase = false;
    QStringList extra = extraArgumentsFor(row, &matchesBase);
    if (!matchesBase)
        extra = QStringList();

    DetectedGame game;
    game.title = row.title;
    game.platform = row.platform;
    game.romPath = row.romPath;
    game.romKind = row.romKind;
    game.size = row.size;
    game.artPath = row.artPath;
    game.sourceNote = QStringLiteral("re-imported in XMB Manager");

    PackageInput input;
    input.game = game;
    input.emulatorId = row.emulatorId;
    input.extraArgs = extra;
    input.collection = row.collection;
    input.replace = true;

    QString error;
    if (!createPackage(settings_, input, *profile, nullptr, error)) {
        libraryMessage_->setText(QStringLiteral("Re-import failed: %1").arg(error));
        libraryMessage_->setVisible(true);
        setStatusMessage(QStringLiteral("Re-import failed: %1").arg(error));
        return;
    }
    libraryMessage_->setVisible(false);
    refreshLibrary();
    refreshDiagnostics();
    setStatusMessage(QStringLiteral("Re-imported '%1'.").arg(row.title));
}

void MainWindow::rebuildTagFilter() {
    if (!tagChipsLayout_ || !tagChipsHost_)
        return;
    updatingTagChips_ = true;
    while (tagChipsLayout_->count() > 0) {
        QLayoutItem* item = tagChipsLayout_->takeAt(0);
        if (!item)
            break;
        if (QWidget* widget = item->widget()) {
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }

    QHash<QString, QPair<QString, int>> byKey;
    for (const LibraryRow& row : library_) {
        for (const QString& tag : row.tags) {
            const QString key = tag.toCaseFolded();
            auto it = byKey.find(key);
            if (it == byKey.end())
                byKey.insert(key, {tag, 1});
            else
                ++it.value().second;
        }
    }
    // A chip whose tag disappeared must not keep filtering the table.
    for (int index = activeTagFilter_.size() - 1; index >= 0; --index) {
        if (!byKey.contains(activeTagFilter_.at(index).toCaseFolded()))
            activeTagFilter_.removeAt(index);
    }

    QStringList keys = byKey.keys();
    std::sort(keys.begin(), keys.end(), [&byKey](const QString& left, const QString& right) {
        return QString::compare(byKey.value(left).first, byKey.value(right).first,
                                Qt::CaseInsensitive) < 0;
    });
    for (const QString& key : keys) {
        const QPair<QString, int> entry = byKey.value(key);
        auto* chip = new QPushButton(QStringLiteral("%1 (%2)").arg(entry.first).arg(entry.second),
                                     tagChipsHost_);
        chip->setObjectName(QStringLiteral("tagChip"));
        chip->setCheckable(true);
        chip->setChecked(activeTagFilter_.contains(entry.first, Qt::CaseInsensitive));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setToolTip(
            QStringLiteral("Filter by the tag '%1' (%2 package(s)). Several chips combine with AND.")
                .arg(entry.first)
                .arg(entry.second));
        chip->setProperty("tagValue", entry.first);
        connect(chip, &QPushButton::clicked, this, [this, chip] {
            setTagFilter(chip->property("tagValue").toString(), chip->isChecked());
        });
        tagChipsLayout_->addWidget(chip);
    }
    tagChipsLayout_->addStretch(1);
    updatingTagChips_ = false;

    if (tagFilterSummary_) {
        if (byKey.isEmpty()) {
            tagFilterSummary_->setText(QStringLiteral("No tags yet"));
            tagFilterSummary_->setToolTip(
                QStringLiteral("Tags are stored in pkg.json; use Edit tags (Ctrl+Shift+T)."));
        } else if (activeTagFilter_.isEmpty()) {
            tagFilterSummary_->setText(
                QStringLiteral("%1 tag(s); click to filter").arg(byKey.size()));
            tagFilterSummary_->setToolTip(
                QStringLiteral("Tag filters combine with the search box and the collection panel."));
        } else {
            tagFilterSummary_->setText(
                QStringLiteral("Filtering: %1").arg(activeTagFilter_.join(QStringLiteral(" + "))));
            tagFilterSummary_->setToolTip(
                QStringLiteral("A package must carry every selected tag (AND)."));
        }
    }
}

void MainWindow::setTagFilter(const QString& tag, bool enabled) {
    if (updatingTagChips_)
        return;
    if (enabled) {
        if (!activeTagFilter_.contains(tag, Qt::CaseInsensitive))
            activeTagFilter_.append(tag);
    } else {
        for (int index = activeTagFilter_.size() - 1; index >= 0; --index) {
            if (activeTagFilter_.at(index).compare(tag, Qt::CaseInsensitive) == 0)
                activeTagFilter_.removeAt(index);
        }
    }
    if (tagFilterSummary_) {
        tagFilterSummary_->setText(
            activeTagFilter_.isEmpty()
                ? QStringLiteral("No tag filter")
                : QStringLiteral("Filtering: %1").arg(activeTagFilter_.join(QStringLiteral(" + "))));
    }
    setUiValue(QStringLiteral("library/activeTags"), activeTagFilter_);
    applyLibraryFilter();
}

void MainWindow::refreshCollections() {
    QHash<QString, QStringList> collections;
    QString error;
    if (readCollections(settings_, collections, error)) {
        collections_ = collections;
        collectionsError_.clear();
    } else {
        collections_.clear();
        collectionsError_ = error;
    }
    rebuildCollectionsList();
}

void MainWindow::rebuildCollectionsList() {
    if (!collectionsList_)
        return;
    {
        QSignalBlocker blocker(collectionsList_);
        collectionsList_->clear();

        int unassigned = 0;
        for (const LibraryRow& row : library_) {
            if (row.collection.trimmed().isEmpty())
                ++unassigned;
        }
        auto* allItem = new QListWidgetItem(
            QStringLiteral("All packages  (%1)").arg(library_.size()), collectionsList_);
        allItem->setData(Qt::UserRole, QString());
        allItem->setToolTip(QStringLiteral("Every package in the library"));
        if (unassigned > 0) {
            auto* noneItem = new QListWidgetItem(
                QStringLiteral("(no collection)  (%1)").arg(unassigned), collectionsList_);
            noneItem->setData(Qt::UserRole, noCollectionFilter());
            noneItem->setToolTip(QStringLiteral("Packages whose collection field is empty"));
        }
        QStringList names = collections_.keys();
        names.sort(Qt::CaseInsensitive);
        for (const QString& name : names) {
            auto* item = new QListWidgetItem(
                QStringLiteral("%1  (%2)").arg(name).arg(collections_.value(name).size()),
                collectionsList_);
            item->setData(Qt::UserRole, name);
            item->setToolTip(
                QStringLiteral("Selecting this collection filters the table. Membership comes "
                               "from collections.json (%1 id(s)).")
                    .arg(collections_.value(name).size()));
        }

        // Fall back to "All packages" when the remembered filter no longer exists.
        if (!collectionFilter_.isEmpty()) {
            bool present = false;
            for (int row = 0; row < collectionsList_->count(); ++row) {
                if (collectionsList_->item(row)->data(Qt::UserRole).toString() == collectionFilter_) {
                    present = true;
                    break;
                }
            }
            if (!present)
                collectionFilter_.clear();
        }
        int selectRow = 0;
        for (int row = 0; row < collectionsList_->count(); ++row) {
            if (collectionsList_->item(row)->data(Qt::UserRole).toString() == collectionFilter_) {
                selectRow = row;
                break;
            }
        }
        if (collectionsList_->count() > 0)
            collectionsList_->setCurrentRow(selectRow);
    }
    if (collectionsMessage_) {
        collectionsMessage_->setText(collectionsError_);
        collectionsMessage_->setVisible(!collectionsError_.isEmpty());
    }
    const bool isReal = !collectionFilter_.isEmpty()
        && collectionFilter_ != noCollectionFilter();
    if (collectionRenameButton_)
        collectionRenameButton_->setEnabled(isReal && !isBusy());
    if (collectionRemoveButton_)
        collectionRemoveButton_->setEnabled(isReal && !isBusy());
    if (collectionAddButton_)
        collectionAddButton_->setEnabled(!isBusy());
}

void MainWindow::onCollectionFilterChanged() {
    const QListWidgetItem* item = collectionsList_->currentItem();
    collectionFilter_ = item ? item->data(Qt::UserRole).toString() : QString();
    const bool isReal = !collectionFilter_.isEmpty()
        && collectionFilter_ != noCollectionFilter();
    if (collectionRenameButton_)
        collectionRenameButton_->setEnabled(isReal && !isBusy());
    if (collectionRemoveButton_)
        collectionRemoveButton_->setEnabled(isReal && !isBusy());
    setUiValue(QStringLiteral("library/collectionFilter"), collectionFilter_);
    applyLibraryFilter();
}

void MainWindow::createCollection() {
    if (isBusy())
        return;
    bool accepted = false;
    const QString typed = QInputDialog::getText(
        this, QStringLiteral("New collection"),
        QStringLiteral("Collection name (1-64 characters). The selected packages are added to "
                       "it."),
        QLineEdit::Normal, QString(), &accepted);
    if (!accepted)
        return;
    const QString name = typed.trimmed();
    if (name.isEmpty()) {
        setStatusMessage(QStringLiteral("A collection name must not be empty."));
        return;
    }
    const QVector<int> indices = selectedLibraryIndices();
    if (indices.size() == 1) {
        // setPackageCollection is the core call that keeps pkg.json and
        // collections.json in step and drops every other membership.
        const QString appId = library_.at(indices.first()).id;
        QString error;
        if (!setPackageCollection(settings_, appId, name, error)) {
            collectionsMessage_->setText(QStringLiteral("Collection failed: %1").arg(error));
            collectionsMessage_->setVisible(true);
            setStatusMessage(QStringLiteral("Collection failed: %1").arg(error));
            return;
        }
        // Keep the current filter so the table does not jump to the new collection
        // and hide the rest of the library; the panel still shows the new entry.
        refreshLibrary();
        refreshDiagnostics();
        setStatusMessage(QStringLiteral("Added %1 to the collection '%2'.").arg(appId, name));
        return;
    }
    if (indices.size() >= 2) {
        QStringList ids;
        for (int index : indices)
            ids.append(library_.at(index).id);
        BulkRequest request;
        request.ids = ids;
        request.setCollection = true;
        request.collection = name;
        runBulk(request, QStringLiteral("Adding %1 package(s) to '%2'")
                             .arg(ids.size())
                             .arg(name),
                false);
        return;
    }
    QHash<QString, QStringList> collections;
    QString error;
    if (!readCollections(settings_, collections, error)) {
        collectionsMessage_->setText(QStringLiteral("collections.json: %1").arg(error));
        collectionsMessage_->setVisible(true);
        return;
    }
    if (!collections.contains(name))
        collections.insert(name, QStringList());
    QString writeError;
    if (!writeCollections(settings_, collections, writeError)) {
        collectionsMessage_->setText(QStringLiteral("Could not write collections.json: %1")
                                         .arg(writeError));
        collectionsMessage_->setVisible(true);
        return;
    }
    refreshLibrary();
    refreshDiagnostics();
    setStatusMessage(QStringLiteral("Created empty collection '%1'; select packages and use Add "
                                    "to collection to fill it.")
                         .arg(name));
}

void MainWindow::renameSelectedCollection() {
    if (isBusy())
        return;
    const QString from = collectionFilter_;
    if (from.isEmpty() || from == noCollectionFilter())
        return;
    bool accepted = false;
    const QString typed = QInputDialog::getText(
        this, QStringLiteral("Rename collection"),
        QStringLiteral("New name for '%1'").arg(from), QLineEdit::Normal, from, &accepted);
    if (!accepted)
        return;
    const QString to = typed.trimmed();
    if (to.isEmpty() || to == from) {
        setStatusMessage(QStringLiteral("The collection name was not changed."));
        return;
    }
    int affected = 0;
    QString error;
    if (!renameCollection(settings_, from, to, affected, error)) {
        collectionsMessage_->setText(QStringLiteral("Rename failed: %1").arg(error));
        collectionsMessage_->setVisible(true);
        setStatusMessage(QStringLiteral("Rename failed: %1").arg(error));
        return;
    }
    collectionFilter_ = to;
    refreshLibrary();
    refreshDiagnostics();
    setStatusMessage(QStringLiteral("Renamed collection '%1' to '%2' (%3 package(s) updated).")
                         .arg(from, to)
                         .arg(affected));
}

void MainWindow::removeSelectedCollection() {
    if (isBusy())
        return;
    const QString name = collectionFilter_;
    if (name.isEmpty() || name == noCollectionFilter())
        return;
    const int members = collections_.value(name).size();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Remove collection"),
        QStringLiteral("Remove the collection '%1'?\n\n%2 member package(s) keep their files, "
                       "registrations, and tags; only the collection field is cleared.")
            .arg(name)
            .arg(members),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    int affected = 0;
    QString error;
    if (!removeCollection(settings_, name, affected, error)) {
        collectionsMessage_->setText(QStringLiteral("Remove failed: %1").arg(error));
        collectionsMessage_->setVisible(true);
        setStatusMessage(QStringLiteral("Remove failed: %1").arg(error));
        return;
    }
    collectionFilter_.clear();
    refreshLibrary();
    refreshDiagnostics();
    setStatusMessage(QStringLiteral("Removed collection '%1' (%2 package(s) updated).")
                         .arg(name)
                         .arg(affected));
}

void MainWindow::assignSelectedToCollection() {
    if (isBusy())
        return;
    const QStringList ids = selectedLibraryIds();
    if (ids.isEmpty()) {
        setStatusMessage(QStringLiteral("Select at least one package first."));
        return;
    }
    QStringList names = collections_.keys();
    names.sort(Qt::CaseInsensitive);
    QStringList items;
    items.append(QStringLiteral("(clear collection)"));
    items.append(names);
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(
        this, QStringLiteral("Add to collection"),
        QStringLiteral("Collection for %1 selected package(s). Pick one or type a new name:")
            .arg(ids.size()),
        items, 0, true, &accepted);
    if (!accepted)
        return;
    const QString name = chosen.trimmed();
    if (name.isEmpty()) {
        setStatusMessage(QStringLiteral("A collection name must not be empty."));
        return;
    }
    const QString target = name == QStringLiteral("(clear collection)") ? QString() : name;
    if (ids.size() == 1) {
        QString error;
        if (!setPackageCollection(settings_, ids.first(), target, error)) {
            collectionsMessage_->setText(QStringLiteral("Collection failed: %1").arg(error));
            collectionsMessage_->setVisible(true);
            setStatusMessage(QStringLiteral("Collection failed: %1").arg(error));
            return;
        }
        // The current filter is left alone: the table follows the filter, not the change.
        refreshLibrary();
        refreshDiagnostics();
        setStatusMessage(target.isEmpty()
                             ? QStringLiteral("Cleared the collection of %1.").arg(ids.first())
                             : QStringLiteral("Added %1 to the collection '%2'.")
                                   .arg(ids.first(), target));
        return;
    }
    BulkRequest request;
    request.ids = ids;
    request.setCollection = true;
    request.collection = target;
    runBulk(request,
            request.collection.isEmpty()
                ? QStringLiteral("Clearing the collection for %1 package(s)").arg(ids.size())
                : QStringLiteral("Adding %1 package(s) to '%2'")
                      .arg(ids.size())
                      .arg(request.collection),
            false);
}

void MainWindow::bulkSetCollection() {
    if (isBusy())
        return;
    const QStringList ids = selectedLibraryIds();
    if (ids.size() < 2)
        return;
    QStringList names = collections_.keys();
    names.sort(Qt::CaseInsensitive);
    QStringList items;
    items.append(QStringLiteral("(clear collection)"));
    items.append(names);
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(
        this, QStringLiteral("Bulk: add to collection"),
        QStringLiteral("Collection for %1 selected package(s). Pick one or type a new name:")
            .arg(ids.size()),
        items, 0, true, &accepted);
    if (!accepted)
        return;
    const QString name = chosen.trimmed();
    BulkRequest request;
    request.ids = ids;
    request.setCollection = true;
    request.collection = name == QStringLiteral("(clear collection)") ? QString() : name;
    runBulk(request,
            request.collection.isEmpty()
                ? QStringLiteral("Clearing the collection for %1 package(s)").arg(ids.size())
                : QStringLiteral("Adding %1 package(s) to '%2'")
                      .arg(ids.size())
                      .arg(request.collection));
}

void MainWindow::bulkSetEmulator() {
    if (isBusy())
        return;
    const QStringList ids = selectedLibraryIds();
    if (ids.size() < 2)
        return;
    if (profiles_.isEmpty()) {
        setStatusMessage(QStringLiteral("No emulator profiles are available."));
        return;
    }
    QStringList labels;
    for (const EmulatorProfile& profile : profiles_) {
        labels.append(QStringLiteral("%1 (%2)")
                          .arg(profile.name.isEmpty() ? profile.id : profile.name, profile.id));
    }
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(
        this, QStringLiteral("Bulk: set emulator"),
        QStringLiteral("Emulator for %1 selected package(s)").arg(ids.size()), labels, 0, false,
        &accepted);
    if (!accepted)
        return;
    const int index = labels.indexOf(chosen);
    if (index < 0 || index >= profiles_.size())
        return;
    BulkRequest request;
    request.ids = ids;
    request.setEmulator = true;
    request.emulatorId = profiles_.at(index).id;
    runBulk(request, QStringLiteral("Setting the emulator to '%1' for %2 package(s)")
                         .arg(profiles_.at(index).id)
                         .arg(ids.size()));
}

void MainWindow::bulkAddTags() {
    if (isBusy())
        return;
    const QStringList ids = selectedLibraryIds();
    if (ids.size() < 2)
        return;
    bool accepted = false;
    const QString typed = QInputDialog::getText(
        this, QStringLiteral("Bulk: add tags"),
        QStringLiteral("Tags to add to %1 package(s), comma-separated").arg(ids.size()),
        QLineEdit::Normal, QString(), &accepted);
    if (!accepted)
        return;
    QStringList tags;
    for (const QString& part : typed.split(QLatin1Char(','))) {
        const QString tag = part.trimmed();
        if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive))
            tags.append(tag);
    }
    if (tags.isEmpty()) {
        setStatusMessage(QStringLiteral("Enter at least one tag."));
        return;
    }
    BulkRequest request;
    request.ids = ids;
    request.addTags = tags;
    runBulk(request, QStringLiteral("Adding tags to %1 package(s)").arg(ids.size()));
}

void MainWindow::bulkRemoveTags() {
    if (isBusy())
        return;
    const QStringList ids = selectedLibraryIds();
    if (ids.size() < 2)
        return;
    bool accepted = false;
    const QString typed = QInputDialog::getText(
        this, QStringLiteral("Bulk: remove tags"),
        QStringLiteral("Tags to remove from %1 package(s), comma-separated").arg(ids.size()),
        QLineEdit::Normal, QString(), &accepted);
    if (!accepted)
        return;
    QStringList tags;
    for (const QString& part : typed.split(QLatin1Char(','))) {
        const QString tag = part.trimmed();
        if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive))
            tags.append(tag);
    }
    if (tags.isEmpty()) {
        setStatusMessage(QStringLiteral("Enter at least one tag."));
        return;
    }
    BulkRequest request;
    request.ids = ids;
    request.removeTags = tags;
    runBulk(request, QStringLiteral("Removing tags from %1 package(s)").arg(ids.size()));
}

void MainWindow::bulkRemovePackages() {
    if (isBusy())
        return;
    const QStringList ids = selectedLibraryIds();
    if (ids.size() < 2)
        return;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Remove packages"),
        QStringLiteral("Remove %1 packages from the XMB library?\n\nTheir apps.json "
                       "registrations and pkg/<id>/ folders are deleted. The ROM files and the "
                       "emulators are never touched.")
            .arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    BulkRequest request;
    request.ids = ids;
    request.remove = true;
    runBulk(request, QStringLiteral("Removing %1 package(s)").arg(ids.size()));
}

void MainWindow::runBulk(const BulkRequest& request, const QString& label, bool showReport) {
    if (request.ids.size() < 2) {
        setStatusMessage(QStringLiteral("A bulk change needs at least two packages."));
        return;
    }
    if (!beginTask(label))
        return;
    bulkShowReport_ = showReport;
    libraryMessage_->setVisible(false);
    const Settings settings = settings_;
    const BulkRequest workerRequest = request;
    taskThread_ = QThread::create([this, settings, workerRequest, label] {
        Settings workerSettings = settings;
        QVector<BulkResult> results;
        QString error;
        const bool applied = applyBulk(workerSettings, workerRequest, results, error);
        QMetaObject::invokeMethod(
            this, [this, applied, results, error, label] { finishBulk(applied, results, error, label); },
            Qt::QueuedConnection);
    });
    connect(taskThread_.data(), &QThread::finished, taskThread_.data(), &QObject::deleteLater);
    taskThread_->start();
}

void MainWindow::finishBulk(bool applied, const QVector<BulkResult>& results,
                            const QString& error, const QString& label) {
    endTask();
    int succeeded = 0;
    int failed = 0;
    QStringList lines;
    for (const BulkResult& result : results) {
        if (result.ok)
            ++succeeded;
        else
            ++failed;
        lines.append(QStringLiteral("%1 | %2 | %3")
                         .arg(result.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
                              result.id,
                              result.action.isEmpty() ? QStringLiteral("(no action)")
                                                      : result.action));
        if (!result.error.isEmpty())
            lines.append(QStringLiteral("    %1").arg(result.error));
    }
    QString message = QStringLiteral("%1: %2 applied, %3 failed.").arg(label).arg(succeeded).arg(
        failed);
    if (!applied) {
        message += QStringLiteral(" The core rolled the whole request back; nothing was changed.");
        libraryMessage_->setText(
            QStringLiteral("%1 %2").arg(message, error.isEmpty() ? QString() : error));
        libraryMessage_->setVisible(true);
    } else {
        libraryMessage_->setVisible(false);
    }
    refreshLibrary();
    refreshDiagnostics();
    setStatusMessage(message);

    if (!bulkShowReport_ && applied && failed == 0) {
        appendImportReport(QStringLiteral("%1: %2 applied, 0 failed.").arg(label).arg(succeeded));
        for (const QString& line : lines)
            appendImportReport(line);
        return;
    }

    QMessageBox box(this);
    box.setIcon(!applied || failed > 0 ? QMessageBox::Warning : QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Bulk change"));
    box.setText(message);
    box.setInformativeText(
        applied ? QStringLiteral("Each package reports its own result below.")
                : QStringLiteral("Rollback: %1").arg(error.isEmpty()
                                                      ? QStringLiteral("the core restored every "
                                                                       "touched file")
                                                      : error));
    box.setDetailedText(lines.join(QLatin1Char('\n')));
    box.exec();
}

void MainWindow::backupLibrary() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current task to finish."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Backup library"));
    dialog.setObjectName(QStringLiteral("backupDialog"));
    dialog.setMinimumWidth(680);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    layout->addWidget(makeSectionLabel(&dialog, QStringLiteral("Write an xmb-manager-backup/1 "
                                                               "bundle")));
    layout->addWidget(makeMutedLabel(
        &dialog,
        QStringLiteral("The bundle carries the game registrations, package records (tags and "
                       "hashes included), collections, the effective platform map, and the "
                       "emulator overlay. It is portable and can be restored on another machine "
                       "with path remaps.")));
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* pathRow = new QWidget(&dialog);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(6);
    auto* pathEdit = new QLineEdit(
        QDir::toNativeSeparators(QDir(settings_.dataDirectory)
                                     .filePath(QStringLiteral("xmb-backup-%1.json")
                                                   .arg(QDateTime::currentDateTime().toString(
                                                       QStringLiteral("yyyyMMdd-HHmmss"))))),
        pathRow);
    pathEdit->setObjectName(QStringLiteral("backupPathEdit"));
    pathLayout->addWidget(pathEdit, 1);
    auto* browseButton = new QPushButton(QStringLiteral("Browse..."), pathRow);
    pathLayout->addWidget(browseButton);
    form->addRow(QStringLiteral("Bundle file"), pathRow);
    layout->addLayout(form);
    auto* artCheck = new QCheckBox(
        QStringLiteral("Embed packaged art (ICON0.PNG, at most 64 KiB each)"), &dialog);
    artCheck->setObjectName(QStringLiteral("backupArtCheck"));
    artCheck->setChecked(false);
    artCheck->setToolTip(
        QStringLiteral("Passes BackupOptions::withPackagedArt to writeBackup. Larger icons are "
                       "skipped and counted in artOmitted."));
    layout->addWidget(artCheck);
    auto* error = makeErrorLabel(&dialog);
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, &dialog, [&] {
        const QString file = QFileDialog::getSaveFileName(
            &dialog, QStringLiteral("Write the backup bundle"),
            QFileInfo(pathEdit->text()).absolutePath(),
            QStringLiteral("XMB Manager backup (*.json);;All files (*)"));
        if (!file.isEmpty())
            pathEdit->setText(QDir::toNativeSeparators(file));
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (pathEdit->text().trimmed().isEmpty()) {
            error->setText(QStringLiteral("Choose a bundle path."));
            error->setVisible(true);
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    BackupOptions options;
    options.withPackagedArt = artCheck->isChecked();
    startBackup(QDir::fromNativeSeparators(pathEdit->text().trimmed()), options);
}

void MainWindow::startBackup(const QString& file, const BackupOptions& options) {
    if (!beginTask(QStringLiteral("Writing the backup")))
        return;
    const Settings settings = settings_;
    const BackupOptions workerOptions = options;
    taskThread_ = QThread::create([this, settings, file, workerOptions] {
        Settings workerSettings = settings;
        QJsonObject summary;
        QString error;
        const bool ok = writeBackup(workerSettings, file, workerOptions, summary, error);
        QMetaObject::invokeMethod(
            this, [this, ok, summary, error, file] { finishBackup(ok, summary, error, file); },
            Qt::QueuedConnection);
    });
    connect(taskThread_.data(), &QThread::finished, taskThread_.data(), &QObject::deleteLater);
    taskThread_->start();
}

void MainWindow::finishBackup(bool ok, const QJsonObject& summary, const QString& error,
                              const QString& file) {
    endTask();
    if (!ok) {
        const QString message = QStringLiteral("Backup failed: %1").arg(error);
        setStatusMessage(message);
        QMessageBox::critical(this, QStringLiteral("Backup library"), message);
        return;
    }
    const QString text =
        QStringLiteral("Wrote %1\n%2 app(s), %3 package(s), %4 collection(s), %5 mapping(s), "
                       "%6 emulator overlay(s), %7 art file(s) (%8 omitted), %9 bytes.")
            .arg(QDir::toNativeSeparators(file))
            .arg(summary.value(QStringLiteral("appCount")).toInt())
            .arg(summary.value(QStringLiteral("packageCount")).toInt())
            .arg(summary.value(QStringLiteral("collectionCount")).toInt())
            .arg(summary.value(QStringLiteral("mappingCount")).toInt())
            .arg(summary.value(QStringLiteral("emulatorOverlayCount")).toInt())
            .arg(summary.value(QStringLiteral("artIncluded")).toInt())
            .arg(summary.value(QStringLiteral("artOmitted")).toInt())
            .arg(summary.value(QStringLiteral("bytes")).toVariant().toLongLong());
    setStatusMessage(QStringLiteral("Backup written: %1").arg(QDir::toNativeSeparators(file)));
    appendImportReport(text);
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Backup library"));
    box.setText(QStringLiteral("Backup written."));
    box.setInformativeText(text);
    box.setDetailedText(
        QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented)));
    box.exec();
}

void MainWindow::restoreLibrary() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current task to finish."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Restore library"));
    dialog.setObjectName(QStringLiteral("restoreDialog"));
    dialog.setMinimumWidth(760);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    layout->addWidget(makeSectionLabel(&dialog, QStringLiteral("Restore an xmb-manager-backup/1 "
                                                               "bundle")));
    layout->addWidget(makeMutedLabel(
        &dialog,
        QStringLiteral("The core validates the whole bundle before writing anything. Path remap "
                       "rows replace that prefix in every restored ROM path, argument, executable, "
                       "working directory, and artwork path - the migrate-to-another-machine path.")));
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* pathRow = new QWidget(&dialog);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(6);
    auto* pathEdit = new QLineEdit(pathRow);
    pathEdit->setObjectName(QStringLiteral("restorePathEdit"));
    pathEdit->setPlaceholderText(QStringLiteral("C:/Backups/xmb-backup.json"));
    pathLayout->addWidget(pathEdit, 1);
    auto* browseButton = new QPushButton(QStringLiteral("Browse..."), pathRow);
    pathLayout->addWidget(browseButton);
    form->addRow(QStringLiteral("Bundle file"), pathRow);
    layout->addLayout(form);

    auto* remapCaption = new QLabel(QStringLiteral("Path remaps (from -> to)"), &dialog);
    remapCaption->setObjectName(QStringLiteral("detailCaption"));
    layout->addWidget(remapCaption);
    auto* remapTable = new QTableWidget(0, 2, &dialog);
    remapTable->setObjectName(QStringLiteral("restoreRemapTable"));
    remapTable->setHorizontalHeaderLabels({QStringLiteral("From"), QStringLiteral("To")});
    remapTable->verticalHeader()->setVisible(false);
    remapTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    remapTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    remapTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    remapTable->setMinimumHeight(110);
    layout->addWidget(remapTable);
    auto* remapButtons = new QHBoxLayout;
    auto* addRowButton = new QPushButton(QStringLiteral("Add row"), &dialog);
    auto* removeRowButton = new QPushButton(QStringLiteral("Remove row"), &dialog);
    remapButtons->addWidget(addRowButton);
    remapButtons->addWidget(removeRowButton);
    remapButtons->addStretch(1);
    layout->addLayout(remapButtons);
    layout->addWidget(makeMutedLabel(
        &dialog,
        QStringLiteral("For example C:/Games -> D:/Games. Leave the table empty to restore the "
                       "original paths.")));
    auto* replaceCheck = new QCheckBox(
        QStringLiteral("Replace packages that already exist in this data folder"), &dialog);
    replaceCheck->setObjectName(QStringLiteral("restoreReplaceCheck"));
    replaceCheck->setToolTip(
        QStringLiteral("Without this, existing package ids are a conflict; the core then refuses "
                       "the whole restore before writing anything."));
    layout->addWidget(replaceCheck);
    auto* error = makeErrorLabel(&dialog);
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, &dialog, [&] {
        const QString file = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("Choose a backup bundle"),
            QFileInfo(pathEdit->text()).absolutePath(),
            QStringLiteral("XMB Manager backup (*.json);;All files (*)"));
        if (!file.isEmpty())
            pathEdit->setText(QDir::toNativeSeparators(file));
    });
    connect(addRowButton, &QPushButton::clicked, &dialog, [remapTable] {
        const int row = remapTable->rowCount();
        remapTable->insertRow(row);
        remapTable->setItem(row, 0, new QTableWidgetItem(QString()));
        remapTable->setItem(row, 1, new QTableWidgetItem(QString()));
        remapTable->setCurrentCell(row, 0);
    });
    connect(removeRowButton, &QPushButton::clicked, &dialog, [remapTable] {
        const int row = remapTable->currentRow();
        if (row >= 0)
            remapTable->removeRow(row);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (pathEdit->text().trimmed().isEmpty()) {
            error->setText(QStringLiteral("Choose a bundle file."));
            error->setVisible(true);
            return;
        }
        for (int row = 0; row < remapTable->rowCount(); ++row) {
            const QString from = remapTable->item(row, 0)
                                     ? remapTable->item(row, 0)->text().trimmed()
                                     : QString();
            const QString to = remapTable->item(row, 1)
                                   ? remapTable->item(row, 1)->text().trimmed()
                                   : QString();
            if (from.isEmpty() && to.isEmpty())
                continue;
            if (from.isEmpty() || to.isEmpty()) {
                error->setText(QStringLiteral("Remap row %1 needs both a from and a to path.")
                                   .arg(row + 1));
                error->setVisible(true);
                return;
            }
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;

    QVector<PathRemap> remaps;
    for (int row = 0; row < remapTable->rowCount(); ++row) {
        const QString from = remapTable->item(row, 0)
                                 ? remapTable->item(row, 0)->text().trimmed()
                                 : QString();
        const QString to = remapTable->item(row, 1)
                               ? remapTable->item(row, 1)->text().trimmed()
                               : QString();
        if (from.isEmpty() && to.isEmpty())
            continue;
        PathRemap remap;
        remap.from = QDir::fromNativeSeparators(from);
        remap.to = QDir::fromNativeSeparators(to);
        remaps.append(remap);
    }
    startRestore(QDir::fromNativeSeparators(pathEdit->text().trimmed()), remaps,
                 replaceCheck->isChecked());
}

void MainWindow::startRestore(const QString& file, const QVector<PathRemap>& remaps, bool replace) {
    if (!beginTask(QStringLiteral("Restoring the backup")))
        return;
    const Settings settings = settings_;
    const QVector<PathRemap> workerRemaps = remaps;
    taskThread_ = QThread::create([this, settings, file, workerRemaps, replace] {
        Settings workerSettings = settings;
        QJsonArray actions;
        QJsonArray conflicts;
        QString error;
        const bool ok = restoreBackup(workerSettings, file, workerRemaps, replace, actions,
                                      conflicts, error);
        QMetaObject::invokeMethod(
            this,
            [this, ok, actions, conflicts, error, file, workerRemaps, replace] {
                finishRestore(ok, actions, conflicts, error, file, workerRemaps, replace);
            },
            Qt::QueuedConnection);
    });
    connect(taskThread_.data(), &QThread::finished, taskThread_.data(), &QObject::deleteLater);
    taskThread_->start();
}

void MainWindow::finishRestore(bool ok, const QJsonArray& actions, const QJsonArray& conflicts,
                               const QString& error, const QString& file,
                               const QVector<PathRemap>& remaps, bool replace) {
    endTask();
    if (!ok) {
        if (!replace && !conflicts.isEmpty()) {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this, QStringLiteral("Restore conflicts"),
                QStringLiteral("%1 package(s) from this bundle already exist in the data "
                               "folder.\n\nReplace them? The core restores the existing package "
                               "directories only when replace is confirmed.")
                    .arg(conflicts.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                startRestore(file, remaps, true);
                return;
            }
            setStatusMessage(QStringLiteral("Restore cancelled; nothing was written."));
            return;
        }
        const QString message = QStringLiteral("Restore failed: %1").arg(error);
        libraryMessage_->setText(message);
        libraryMessage_->setVisible(true);
        setStatusMessage(message);
        QMessageBox box(this);
        box.setIcon(QMessageBox::Critical);
        box.setWindowTitle(QStringLiteral("Restore library"));
        box.setText(message);
        if (!conflicts.isEmpty()) {
            QStringList lines;
            for (const QJsonValue& value : conflicts) {
                const QJsonObject conflict = value.toObject();
                lines.append(QStringLiteral("conflict | %1 | package: %2 | registration: %3")
                                 .arg(conflict.value(QStringLiteral("id")).toString(),
                                      conflict.value(QStringLiteral("packageExists")).toBool()
                                          ? QStringLiteral("yes")
                                          : QStringLiteral("no"),
                                      conflict.value(QStringLiteral("registrationExists")).toBool()
                                          ? QStringLiteral("yes")
                                          : QStringLiteral("no")));
            }
            box.setDetailedText(lines.join(QLatin1Char('\n')));
        }
        box.exec();
        return;
    }
    libraryMessage_->setVisible(false);
    // Restore rewrites every ROM path, so earlier verification results no longer apply.
    lastHashChecks_.clear();
    lastHashCheckTime_.clear();
    // The bundle carries an emulator overlay, so reload the profiles before any
    // validation: otherwise every restored package looks like it references an
    // unknown emulator.
    refreshProfiles();
    populateEmulatorTable(editingProfileId_);
    reinstallDefaultEmulatorCombos();
    updateImportProfileCombos();
    refreshLibrary();
    refreshPlatformMap();
    refreshDiagnostics();
    updateHeaderStatus();
    updateBusyUi();
    showRestoreSummary(actions, file);
}

void MainWindow::showRestoreSummary(const QJsonArray& actions, const QString& file) {
    int created = 0;
    int replaced = 0;
    int art = 0;
    QStringList missingRoms;
    QStringList lines;
    for (const QJsonValue& value : actions) {
        const QJsonObject action = value.toObject();
        const QString kind = action.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("package")) {
            const QString what = action.value(QStringLiteral("action")).toString();
            if (what == QStringLiteral("created"))
                ++created;
            else if (what == QStringLiteral("replaced"))
                ++replaced;
            lines.append(QStringLiteral("package | %1 | %2 | %3")
                             .arg(action.value(QStringLiteral("id")).toString(), what,
                                  action.value(QStringLiteral("romPath")).toString()));
        } else if (kind == QStringLiteral("summary")) {
            art = action.value(QStringLiteral("art")).toInt();
            for (const QJsonValue& rom : action.value(QStringLiteral("missingRoms")).toArray())
                missingRoms.append(rom.toString());
        } else if (kind == QStringLiteral("collection")) {
            lines.append(QStringLiteral("collection | %1 | %2 | %3 id(s)")
                             .arg(action.value(QStringLiteral("name")).toString(),
                                  action.value(QStringLiteral("action")).toString())
                             .arg(action.value(QStringLiteral("ids")).toInt()));
        } else {
            lines.append(QStringLiteral("%1 | %2 | %3")
                             .arg(kind,
                                  action.value(QStringLiteral("id")).toString(),
                                  action.value(QStringLiteral("action")).toString()));
        }
    }
    for (const QString& rom : missingRoms)
        lines.append(QStringLiteral("missing ROM | %1").arg(rom));

    const QString summary =
        QStringLiteral("Restored %1 package(s): %2 created, %3 replaced, %4 missing ROM(s), "
                       "%5 embedded art file(s).")
            .arg(created + replaced)
            .arg(created)
            .arg(replaced)
            .arg(missingRoms.size())
            .arg(art);
    setStatusMessage(summary);
    appendImportReport(QStringLiteral("Restore %1: %2").arg(QDir::toNativeSeparators(file),
                                                           summary));
    QMessageBox box(this);
    box.setIcon(missingRoms.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Restore library"));
    box.setText(summary);
    box.setInformativeText(
        missingRoms.isEmpty()
            ? QStringLiteral("Every restored ROM path exists.")
            : QStringLiteral("A remap row can point the missing ROMs at this machine's copy."));
    box.setDetailedText(lines.join(QLatin1Char('\n')));
    box.exec();
}

QString MainWindow::reportPath() const {
    return QDir(settings_.dataDirectory).filePath(QStringLiteral("xmb-manager-report-2.json"));
}

void MainWindow::copyReportPath() {
    const QString path = QDir::toNativeSeparators(reportPath());
    QApplication::clipboard()->setText(path);
    setStatusMessage(QStringLiteral("Report path copied: %1").arg(path));
}

void MainWindow::showLibraryContextMenu(const QPoint& position) {
    const int tableRow = libraryTable_->rowAt(position.y());
    if (tableRow < 0)
        return;
    const QTableWidgetItem* titleItem = libraryTable_->item(tableRow, 0);
    if (!titleItem || !titleItem->data(Qt::UserRole).isValid())
        return;
    if (!titleItem->isSelected())
        libraryTable_->selectRow(tableRow);
    QMenu menu(this);
    QAction* launch = menu.addAction(QStringLiteral("Test launch"));
    QAction* argv = menu.addAction(QStringLiteral("Show argv"));
    menu.addSeparator();
    QAction* edit = menu.addAction(QStringLiteral("Edit..."));
    QAction* editTags = menu.addAction(QStringLiteral("Edit tags..."));
    QAction* relink = menu.addAction(QStringLiteral("Relink..."));
    QAction* verify = menu.addAction(QStringLiteral("Verify hash"));
    QAction* reimport = menu.addAction(QStringLiteral("Re-import..."));
    menu.addSeparator();
    QAction* collection = menu.addAction(QStringLiteral("Add to collection..."));
    menu.addSeparator();
    QAction* remove = menu.addAction(QStringLiteral("Remove..."));
    const bool hasSelection = selectedLibraryIndex() >= 0;
    launch->setEnabled(hasSelection && !isBusy());
    argv->setEnabled(hasSelection && !isBusy());
    edit->setEnabled(editButton_->isEnabled());
    editTags->setEnabled(editTagsButton_->isEnabled());
    relink->setEnabled(relinkButton_->isEnabled());
    verify->setEnabled(verifyButton_->isEnabled());
    reimport->setEnabled(reimportButton_->isEnabled());
    collection->setEnabled(hasSelection && !isBusy());
    remove->setEnabled(hasSelection && !isBusy());
    QAction* chosen = menu.exec(libraryTable_->viewport()->mapToGlobal(position));
    if (chosen == launch)
        launchLibraryEntry();
    else if (chosen == argv)
        showArgvForSelected();
    else if (chosen == edit)
        editLibraryEntry();
    else if (chosen == editTags)
        editLibraryTags();
    else if (chosen == relink)
        relinkSelectedPackage();
    else if (chosen == verify)
        verifySelectedHashes();
    else if (chosen == reimport)
        reimportLibraryEntry();
    else if (chosen == collection)
        assignSelectedToCollection();
    else if (chosen == remove)
        removeLibraryEntry();
}

void MainWindow::addFiles() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add ROM files"), lastBrowseDirectory(), romFilter());
    if (files.isEmpty())
        return;
    lastBrowseDirectory_ = QFileInfo(files.first()).absolutePath();
    setUiValue(QStringLiteral("paths/lastBrowse"), lastBrowseDirectory_);
    addScannedPaths(files);
}

void MainWindow::addFolder() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Add a folder to scan"), lastBrowseDirectory(),
        QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;
    lastBrowseDirectory_ = directory;
    setUiValue(QStringLiteral("paths/lastBrowse"), lastBrowseDirectory_);
    addScannedPaths(QStringList{directory});
}

void MainWindow::addScannedPaths(const QStringList& paths) {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    QStringList existing;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.exists() && !existing.contains(info.absoluteFilePath(), Qt::CaseInsensitive))
            existing.append(info.absoluteFilePath());
    }
    if (existing.isEmpty()) {
        importMessage_->setText(
            QStringLiteral("Those paths do not exist, or are not readable files or folders."));
        return;
    }
    if (tabs_ && importTab_)
        tabs_->setCurrentWidget(importTab_);
    importMessage_->clear();
    startScan(existing);
}

void MainWindow::startScan(const QStringList& roots) {
    if (isBusy())
        return;
    scanRunning_ = true;
    updateBusyUi();
    setImportProgress(0, 0, QStringLiteral("Scanning..."));
    importMessage_->setText(
        QStringLiteral("Scanning %1 path(s)...").arg(roots.size()));
    importReport_->appendPlainText(
        QStringLiteral("Scan started for %1 path(s).").arg(roots.size()));
    const QStringList scanRoots = roots;
    scanThread_ = QThread::create([this, scanRoots] {
        QStringList warnings;
        const QVector<DetectedGame> games = scanPaths(scanRoots, &warnings);
        QMetaObject::invokeMethod(
            this, [this, games, warnings] { finishScan(games, warnings); },
            Qt::QueuedConnection);
    });
    connect(scanThread_.data(), &QThread::finished, scanThread_.data(), &QObject::deleteLater);
    scanThread_->start();
}

void MainWindow::finishScan(const QVector<DetectedGame>& games, const QStringList& warnings) {
    scanRunning_ = false;
    importProgress_->setVisible(false);
    int added = 0;
    for (const DetectedGame& game : games) {
        bool duplicate = false;
        for (const ImportRow& row : importRows_) {
            if (QFileInfo(row.game.romPath).absoluteFilePath().compare(
                    QFileInfo(game.romPath).absoluteFilePath(), Qt::CaseInsensitive)
                == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        ImportRow row;
        row.game = game;
        row.emulatorId = emulatorForPlatform(game.platform);
        row.selected = true;
        importRows_.append(row);
        ++added;
    }
    for (const QString& warning : warnings)
        importReport_->appendPlainText(QStringLiteral("Scan warning: %1").arg(warning));
    rebuildImportTable();

    if (added > 0) {
        importMessage_->setText(
            QStringLiteral("Added %1 game(s). Review the emulator choices, then Import selected.")
                .arg(added));
        setStatusMessage(QStringLiteral("Scan finished: %1 game(s) added.").arg(added));
    } else if (!games.isEmpty()) {
        importMessage_->setText(QStringLiteral("Those games are already in the import list."));
        setStatusMessage(QStringLiteral("Scan finished: no new games."));
    } else {
        importMessage_->setText(
            warnings.isEmpty()
                ? QStringLiteral("No supported ROMs or disc images were found.")
                : QStringLiteral("No supported ROMs were found; see the import report."));
        setStatusMessage(QStringLiteral("Scan finished: no games found."));
    }
    updateBusyUi();
    importTable_->setFocus();
}

void MainWindow::rebuildImportTable() {
    if (!importTable_)
        return;
    rebuildingImportTable_ = true;
    importTable_->setRowCount(importRows_.size());
    // Resolution reads the map and the profiles; cache it per platform so a large
    // import list does not re-read those files once per row.
    QHash<QString, QString> emulatorByPlatform;
    QHash<QString, QString> sourceByPlatform;
    const auto resolvedEmulator = [this, &emulatorByPlatform](const QString& platform) {
        const auto found = emulatorByPlatform.constFind(platform);
        if (found != emulatorByPlatform.constEnd())
            return found.value();
        const QString value = emulatorForPlatform(platform);
        emulatorByPlatform.insert(platform, value);
        return value;
    };
    const auto resolvedSource = [this, &sourceByPlatform](const QString& platform) {
        if (platform.isEmpty())
            return QString();
        const auto found = sourceByPlatform.constFind(platform);
        if (found != sourceByPlatform.constEnd())
            return found.value();
        const QString value = mappingSourceFor(platform);
        sourceByPlatform.insert(platform, value);
        return value;
    };
    for (int row = 0; row < importRows_.size(); ++row) {
        const ImportRow& entry = importRows_.at(row);
        auto* titleItem = new QTableWidgetItem(entry.game.title);
        titleItem->setFlags(titleItem->flags() | Qt::ItemIsUserCheckable);
        titleItem->setCheckState(entry.selected ? Qt::Checked : Qt::Unchecked);
        titleItem->setToolTip(entry.game.romPath);
        auto* platformItem = new QTableWidgetItem(entry.game.platform);
        auto* kindItem = new QTableWidgetItem(entry.game.romKind);
        auto* sizeItem = new QTableWidgetItem(
            entry.game.romKind == QStringLiteral("folder") ? QStringLiteral("-")
                                                           : formatBytes(entry.game.size));
        auto* pathItem = new QTableWidgetItem(QDir::toNativeSeparators(entry.game.romPath));
        pathItem->setToolTip(entry.game.romPath);
        importTable_->setItem(row, 0, titleItem);
        importTable_->setItem(row, 1, platformItem);
        importTable_->setItem(row, 2, kindItem);
        importTable_->setItem(row, 3, sizeItem);
        importTable_->setItem(row, 4, pathItem);

        auto* combo = new QComboBox(importTable_);
        if (profiles_.isEmpty()) {
            combo->addItem(QStringLiteral("(no profiles)"), QString());
            combo->setEnabled(false);
        } else {
            for (const EmulatorProfile& profile : profiles_)
                combo->addItem(profile.name.isEmpty() ? profile.id : profile.name, profile.id);
            int index = combo->findData(entry.emulatorId);
            if (index < 0 && !entry.game.platform.isEmpty())
                index = combo->findData(resolvedEmulator(entry.game.platform));
            if (index < 0)
                index = 0;
            combo->setCurrentIndex(index);
            importRows_[row].emulatorId = combo->currentData().toString();
        }
        const QString source = resolvedSource(entry.game.platform);
        combo->setToolTip(
            source == QStringLiteral("map")
                ? QStringLiteral("Preselected by platform-map.json. The map's argument override "
                                 "is used unless you pick a different profile.")
                : (source == QStringLiteral("profile fallback")
                       ? QStringLiteral("No map entry; this profile lists the platform. Its "
                                        "argument template is used.")
                       : QStringLiteral("The platform is unmapped; importing it fails with the "
                                        "core's reason unless you pick a profile.")));
        combo->setProperty("importRow", row);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo](int) {
            if (rebuildingImportTable_)
                return;
            const int tableRow = combo->property("importRow").toInt();
            if (tableRow < 0 || tableRow >= importRows_.size())
                return;
            if (importTable_->cellWidget(tableRow, 5) != combo)
                return;
            importRows_[tableRow].emulatorId = combo->currentData().toString();
            importRows_[tableRow].emulatorUserChosen = true;
            refreshImportSelectionState();
        });
        importTable_->setCellWidget(row, 5, combo);

        auto* resultItem = new QTableWidgetItem(entry.result);
        if (entry.resultKind == kResultImported)
            resultItem->setForeground(QColor(QStringLiteral("#8fd18f")));
        else if (entry.resultKind == kResultDuplicate)
            resultItem->setForeground(QColor(QStringLiteral("#e8bd6a")));
        else if (entry.resultKind == kResultError)
            resultItem->setForeground(QColor(QStringLiteral("#e58a8a")));
        resultItem->setToolTip(entry.result);
        importTable_->setItem(row, 6, resultItem);
    }
    rebuildingImportTable_ = false;
    refreshImportSelectionState();
}

void MainWindow::refreshImportSelectionState() {
    int selected = 0;
    for (const ImportRow& row : importRows_) {
        if (row.selected)
            ++selected;
    }
    importCountLabel_->setText(
        QStringLiteral("%1 detected game(s), %2 selected").arg(importRows_.size()).arg(selected));
    if (importButton_)
        importButton_->setEnabled(!isBusy() && selected > 0);
}

QString MainWindow::emulatorForPlatform(const QString& platform) const {
    if (!platform.isEmpty()) {
        Settings resolveSettings = settings_;
        QString reason;
        const QString mapped = resolveEmulatorForPlatform(resolveSettings, platform, &reason);
        if (!mapped.isEmpty())
            return mapped;
    }
    const QString stored = storedDefaultEmulator(platform);
    if (!stored.isEmpty() && findProfile(profiles_, stored))
        return stored;
    if (!platform.isEmpty()) {
        for (const EmulatorProfile& profile : profiles_) {
            if (profile.platforms.contains(platform, Qt::CaseInsensitive))
                return profile.id;
        }
    }
    if (profiles_.size() == 1)
        return profiles_.first().id;
    return QString();
}

QString MainWindow::storedDefaultEmulator(const QString& platform) const {
    if (platform.isEmpty())
        return QString();
    auto it = defaultEmulators_.constFind(platform);
    if (it != defaultEmulators_.constEnd())
        return it.value();
    return uiValue(QStringLiteral("defaultEmulator/%1").arg(prefPlatformKey(platform)))
        .toString();
}

void MainWindow::setImportProgress(int value, int maximum, const QString& text) {
    if (!importProgress_)
        return;
    importProgress_->setVisible(true);
    importProgress_->setRange(0, maximum);
    importProgress_->setValue(value);
    importProgress_->setFormat(maximum == 0 ? QStringLiteral("Working...")
                                            : QStringLiteral("%v / %m"));
    importProgress_->setToolTip(text);
    importProgress_->setAccessibleDescription(text);
}

void MainWindow::setImportBusy(bool busy) {
    importRunning_ = busy;
    updateBusyUi();
}

void MainWindow::importSelected() {
    if (isBusy())
        return;
    QVector<int> selected;
    for (int index = 0; index < importRows_.size(); ++index) {
        if (importRows_.at(index).selected && importRows_.at(index).resultKind != kResultImported)
            selected.append(index);
    }
    if (selected.isEmpty()) {
        importMessage_->setText(QStringLiteral("Check at least one game to import."));
        return;
    }
    if (profiles_.isEmpty()) {
        importMessage_->setText(
            QStringLiteral("No emulator profiles are available. Add one in the Emulators tab."));
        return;
    }
    const QString collection = importCollectionEdit_->text().trimmed();
    if (collection.size() > 64) {
        importMessage_->setText(QStringLiteral("Collection names are limited to 64 characters."));
        return;
    }
    setUiValue(QStringLiteral("import/collection"), importCollectionEdit_->text());

    struct Job {
        int index = -1;
        DetectedGame game;
        QString emulatorId;
        QStringList argumentsOverride;
        bool emulatorFromMapping = false;
        QString setupError;
    };
    QVector<Job> jobs;
    jobs.reserve(selected.size());
    for (int index : selected) {
        const ImportRow& row = importRows_.at(index);
        Job job;
        job.index = index;
        job.game = row.game;
        job.emulatorId = row.emulatorId;
        if (!row.emulatorUserChosen) {
            // No explicit choice: resolve the platform through the core's map so a mapping's
            // argument override is applied exactly as the CLI would apply it.
            Settings resolveSettings = settings_;
            QString resolvedId;
            QStringList resolvedOverride;
            bool fromMap = false;
            QString reason;
            if (!resolvePlatformTarget(resolveSettings, row.game.platform, resolvedId,
                                       resolvedOverride, fromMap, reason)) {
                job.setupError = reason;
            } else if (resolvedId.isEmpty()) {
                job.setupError = QStringLiteral("No emulator for platform '%1': %2")
                                     .arg(row.game.platform, reason);
            } else {
                job.emulatorId = resolvedId;
                job.argumentsOverride = resolvedOverride;
                job.emulatorFromMapping = fromMap;
            }
        }
        jobs.append(job);
        importRows_[index].result.clear();
        importRows_[index].resultKind = kResultPending;
    }
    rebuildImportTable();

    importRunning_ = true;
    importCancel_.store(false);
    updateBusyUi();
    setImportProgress(0, jobs.size(), QStringLiteral("Importing"));
    importMessage_->setText(QStringLiteral("Importing 0 of %1...").arg(jobs.size()));
    const Settings settings = settings_;
    const QVector<EmulatorProfile> profiles = profiles_;
    const int total = jobs.size();
    const QVector<Job> queued = jobs;
    importThread_ = QThread::create([this, queued, settings, profiles, collection, total] {
        Settings workerSettings = settings;
        for (int position = 0; position < queued.size(); ++position) {
            const Job job = queued.at(position);
            if (importCancel_.load()) {
                QMetaObject::invokeMethod(
                    this,
                    [this, index = job.index, position, total] {
                        importRows_[index].resultKind = kResultError;
                        importRows_[index].result = QStringLiteral("Cancelled");
                        importProgress_->setValue(position + 1);
                    },
                    Qt::QueuedConnection);
                continue;
            }
            const EmulatorProfile* profile = findProfile(profiles, job.emulatorId);
            QString error;
            bool ok = false;
            QString packageId;
            if (!job.setupError.isEmpty()) {
                error = job.setupError;
            } else if (!profile) {
                error = QStringLiteral("Emulator profile '%1' was not found.")
                            .arg(job.emulatorId.isEmpty() ? QStringLiteral("(none)")
                                                          : job.emulatorId);
            } else {
                PackageInput input;
                input.game = job.game;
                input.emulatorId = job.emulatorId;
                input.argumentsOverride = job.argumentsOverride;
                input.emulatorFromMapping = job.emulatorFromMapping;
                input.collection = collection;
                input.replace = false;
                ok = createPackage(workerSettings, input, *profile, nullptr, error);
                if (ok)
                    packageId = packageIdFor(job.game.romPath, job.game.platform);
            }
            QMetaObject::invokeMethod(
                this,
                [this, index = job.index, position, total, ok, error, packageId,
                 fromMap = job.emulatorFromMapping, title = job.game.title] {
                    ImportRow& row = importRows_[index];
                    const QString lowered = error.toLower();
                    if (ok) {
                        row.resultKind = kResultImported;
                        row.result = fromMap
                            ? QStringLiteral("Imported (emulator from the platform map)")
                            : QStringLiteral("Imported");
                        appendImportReport(
                            QStringLiteral("%1: imported as %2%3")
                                .arg(title, packageId,
                                     fromMap ? QStringLiteral(" (platform map)")
                                             : QString()));
                    } else if (lowered.contains(QStringLiteral("already imported"))
                               || lowered.contains(QStringLiteral("already has an entry"))
                               || lowered.contains(QStringLiteral("already exists"))) {
                        row.resultKind = kResultDuplicate;
                        row.result = QStringLiteral("Duplicate refused: %1").arg(error);
                        appendImportReport(QStringLiteral("%1: duplicate refused: %2")
                                               .arg(title, error));
                    } else {
                        row.resultKind = kResultError;
                        row.result = QStringLiteral("Error: %1").arg(error);
                        appendImportReport(
                            QStringLiteral("%1: error: %2").arg(title, error));
                    }
                    if (QTableWidgetItem* item = importTable_->item(index, 6)) {
                        item->setText(row.result);
                        item->setToolTip(row.result);
                        if (row.resultKind == kResultImported)
                            item->setForeground(QColor(QStringLiteral("#8fd18f")));
                        else if (row.resultKind == kResultDuplicate)
                            item->setForeground(QColor(QStringLiteral("#e8bd6a")));
                        else
                            item->setForeground(QColor(QStringLiteral("#e58a8a")));
                    }
                    importProgress_->setValue(position + 1);
                    importMessage_->setText(
                        QStringLiteral("Importing %1 of %2...").arg(position + 1).arg(total));
                },
                Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(
            this, [this, total] { finishImport(); }, Qt::QueuedConnection);
    });
    connect(importThread_.data(), &QThread::finished, importThread_.data(),
            &QObject::deleteLater);
    importThread_->start();
}

void MainWindow::importRealPkg() {
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import a real PS3 package"), lastBrowseDirectory(),
        QStringLiteral("PS3 packages (*.pkg);;All files (*)"));
    if (file.isEmpty())
        return;
    lastBrowseDirectory_ = QFileInfo(file).absolutePath();
    setUiValue(QStringLiteral("paths/lastBrowse"), lastBrowseDirectory_);
    startRealPkgImport(file);
}

void MainWindow::startRealPkgImport(const QString& path) {
    importRunning_ = true;
    importCancel_.store(false);
    updateBusyUi();
    setImportProgress(0, 0, QStringLiteral("Probing package"));
    importMessage_->setText(
        QStringLiteral("Probing %1...").arg(QDir::toNativeSeparators(path)));
    const Settings settings = settings_;
    importThread_ = QThread::create([this, settings, path] {
        Settings workerSettings = settings;
        QString error;
        QStringList notes;
        const bool ok = xmb::manager::importRealPkg(workerSettings, path, error, &notes);
        QMetaObject::invokeMethod(
            this, [this, ok, error, notes, path] { finishRealPkgImport(ok, error, notes, path); },
            Qt::QueuedConnection);
    });
    connect(importThread_.data(), &QThread::finished, importThread_.data(),
            &QObject::deleteLater);
    importThread_->start();
}

void MainWindow::finishImport() {
    importRunning_ = false;
    importProgress_->setVisible(false);
    updateBusyUi();
    refreshLibrary();
    refreshDiagnostics();
    int imported = 0;
    int duplicates = 0;
    int errors = 0;
    for (const ImportRow& row : importRows_) {
        if (row.resultKind == kResultImported)
            ++imported;
        else if (row.resultKind == kResultDuplicate)
            ++duplicates;
        else if (row.resultKind == kResultError)
            ++errors;
    }
    importMessage_->setText(
        QStringLiteral("Import finished: %1 imported, %2 duplicate, %3 error(s).")
            .arg(imported)
            .arg(duplicates)
            .arg(errors));
    setStatusMessage(importMessage_->text());
}

void MainWindow::finishRealPkgImport(bool ok, const QString& error, const QStringList& notes,
                                     const QString& path) {
    importRunning_ = false;
    importProgress_->setVisible(false);
    updateBusyUi();
    for (const QString& note : notes)
        appendImportReport(note);
    if (ok) {
        appendImportReport(QStringLiteral("Imported real package: %1")
                               .arg(QDir::toNativeSeparators(path)));
        importMessage_->setText(QStringLiteral("Real package imported. See the import report."));
        setStatusMessage(QStringLiteral("Real package imported."));
        refreshLibrary();
        refreshDiagnostics();
    } else {
        appendImportReport(QStringLiteral("Real package refused: %1").arg(error));
        importMessage_->setText(QStringLiteral("Real package refused: %1").arg(error));
        setStatusMessage(QStringLiteral("Real package refused: %1").arg(error));
    }
}

void MainWindow::populateEmulatorTable(const QString& selectId) {
    if (!emulatorTable_)
        return;
    const QString keep = selectId.isEmpty() ? editingProfileId_ : selectId;
    loadingEmulatorForm_ = true;
    emulatorTable_->setRowCount(0);
    emulatorTable_->setRowCount(profiles_.size());
    for (int row = 0; row < profiles_.size(); ++row) {
        const EmulatorProfile& profile = profiles_.at(row);
        auto* origin = new QTableWidgetItem(profile.builtIn ? QStringLiteral("Built-in")
                                                            : QStringLiteral("User"));
        origin->setData(Qt::UserRole, profile.id);
        auto* name = new QTableWidgetItem(profile.name);
        auto* platforms = new QTableWidgetItem(profile.platforms.join(QStringLiteral(", ")));
        auto* executable = new QTableWidgetItem(
            profile.executable.isEmpty() ? QStringLiteral("-")
                                         : QDir::toNativeSeparators(profile.executable));
        executable->setToolTip(profile.executable);
        auto* arguments = new QTableWidgetItem(profile.argumentTemplate);
        arguments->setToolTip(profile.argumentTemplate);
        if (profile.builtIn)
            origin->setForeground(QColor(QStringLiteral("#9fb6cf")));
        emulatorTable_->setItem(row, 0, origin);
        emulatorTable_->setItem(row, 1, name);
        emulatorTable_->setItem(row, 2, platforms);
        emulatorTable_->setItem(row, 3, executable);
        emulatorTable_->setItem(row, 4, arguments);
        if (!profile.executable.isEmpty() && !QFileInfo(profile.executable).isFile())
            executable->setForeground(QColor(QStringLiteral("#e58a8a")));
    }
    loadingEmulatorForm_ = false;
    int selectRow = -1;
    for (int row = 0; row < profiles_.size(); ++row) {
        if (profiles_.at(row).id.compare(keep, Qt::CaseInsensitive) == 0) {
            selectRow = row;
            break;
        }
    }
    if (selectRow < 0 && emulatorTable_->rowCount() > 0
        && editingProfileId_.isEmpty())
        selectRow = 0;
    if (selectRow >= 0) {
        emulatorTable_->selectRow(selectRow);
        editingProfileId_ = profiles_.at(selectRow).id;
        loadEmulatorForm(profiles_.at(selectRow));
    } else {
        updateEmulatorButtons();
    }
}

int MainWindow::selectedEmulatorIndex() const {
    if (!emulatorTable_)
        return -1;
    const int row = emulatorTable_->currentRow();
    if (row < 0 || row >= profiles_.size() || emulatorTable_->selectedItems().isEmpty())
        return -1;
    return row;
}

void MainWindow::onEmulatorSelectionChanged() {
    if (loadingEmulatorForm_)
        return;
    const int index = selectedEmulatorIndex();
    if (index < 0) {
        updateEmulatorButtons();
        return;
    }
    const EmulatorProfile& profile = profiles_.at(index);
    editingProfileId_ = profile.id;
    loadEmulatorForm(profile);
    updateEmulatorButtons();
}

void MainWindow::updateEmulatorButtons() {
    const int index = selectedEmulatorIndex();
    const bool has = index >= 0 && index < profiles_.size();
    const bool builtIn = has && profiles_.at(index).builtIn;
    if (emulatorRemoveButton_) {
        emulatorRemoveButton_->setEnabled(has && !builtIn && !isBusy());
        emulatorRemoveButton_->setToolTip(
            builtIn ? QStringLiteral("Built-in profiles cannot be removed; clear or change their "
                                     "executable instead")
                    : QStringLiteral("Remove the selected user profile"));
    }
    if (emulatorSaveButton_)
        emulatorSaveButton_->setEnabled(!isBusy());
    if (emulatorIdEdit_)
        emulatorIdEdit_->setReadOnly(has);
}

void MainWindow::clearEmulatorForm() {
    loadingEmulatorForm_ = true;
    editingProfileId_.clear();
    emulatorIdEdit_->clear();
    emulatorIdEdit_->setReadOnly(false);
    emulatorNameEdit_->clear();
    emulatorExecutableEdit_->clear();
    emulatorWorkingDirectoryEdit_->clear();
    emulatorPlatformsEdit_->clear();
    emulatorTemplateEdit_->setText(QStringLiteral("{rom}"));
    emulatorEnvEdit_->clear();
    loadingEmulatorForm_ = false;
    emulatorStatus_->setText(
        QStringLiteral("New user profile. Fill in an id, name, and executable, then Save profile."));
    emulatorStatus_->setStyleSheet(QString());
    emulatorPreview_->clear();
    emulatorPreviewError_->setVisible(false);
    emulatorLaunchResult_->clear();
    updateEmulatorButtons();
}

void MainWindow::loadEmulatorForm(const EmulatorProfile& profile) {
    loadingEmulatorForm_ = true;
    emulatorIdEdit_->setText(profile.id);
    emulatorIdEdit_->setReadOnly(true);
    emulatorNameEdit_->setText(profile.name);
    emulatorExecutableEdit_->setText(QDir::toNativeSeparators(profile.executable));
    emulatorWorkingDirectoryEdit_->setText(
        QDir::toNativeSeparators(profile.workingDirectory));
    emulatorPlatformsEdit_->setText(profile.platforms.join(QStringLiteral(", ")));
    emulatorTemplateEdit_->setText(profile.argumentTemplate);
    emulatorEnvEdit_->setPlainText(environmentText(profile.env));
    loadingEmulatorForm_ = false;
    emulatorStatus_->setStyleSheet(QString());
    emulatorStatus_->setText(
        profile.builtIn
            ? QStringLiteral("Built-in profile. Set the executable (and working directory if "
                             "needed), then Save profile.")
            : QStringLiteral("User profile. Changes apply when you press Save profile."));
    updateArgumentPreview();
    updateEmulatorButtons();
}

bool MainWindow::readEmulatorForm(EmulatorProfile& profile, QString& error) const {
    error.clear();
    profile = EmulatorProfile();
    profile.id = emulatorIdEdit_->text().trimmed();
    profile.name = emulatorNameEdit_->text().trimmed();
    profile.executable = QDir::fromNativeSeparators(emulatorExecutableEdit_->text().trimmed());
    profile.workingDirectory =
        QDir::fromNativeSeparators(emulatorWorkingDirectoryEdit_->text().trimmed());
    profile.argumentTemplate = emulatorTemplateEdit_->text();
    if (profile.id.isEmpty()) {
        error = QStringLiteral("Profile id must not be empty.");
        return false;
    }
    if (profile.name.isEmpty()) {
        error = QStringLiteral("Profile name must not be empty.");
        return false;
    }
    QStringList platforms;
    for (const QString& part : emulatorPlatformsEdit_->text().split(QLatin1Char(','))) {
        const QString platform = part.trimmed();
        if (!platform.isEmpty() && !platforms.contains(platform))
            platforms.append(platform);
    }
    profile.platforms = platforms;
    if (!parseEnvironmentText(emulatorEnvEdit_->toPlainText(), profile.env, error))
        return false;
    if (!editingProfileId_.isEmpty()) {
        if (const EmulatorProfile* existing = findProfile(profiles_, editingProfileId_))
            profile.builtIn = existing->builtIn;
    }
    return true;
}

void MainWindow::addEmulatorProfile() {
    if (isBusy())
        return;
    emulatorTable_->clearSelection();
    editingProfileId_.clear();
    clearEmulatorForm();
    emulatorIdEdit_->setFocus();
}

void MainWindow::saveEmulatorProfile() {
    if (isBusy())
        return;
    EmulatorProfile profile;
    QString error;
    if (!readEmulatorForm(profile, error)) {
        emulatorStatus_->setText(error);
        emulatorStatus_->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
        setStatusMessage(QStringLiteral("Profile not saved: %1").arg(error));
        return;
    }
    int existingIndex = -1;
    if (!editingProfileId_.isEmpty()) {
        for (int index = 0; index < profiles_.size(); ++index) {
            if (profiles_.at(index).id.compare(editingProfileId_, Qt::CaseInsensitive) == 0) {
                existingIndex = index;
                break;
            }
        }
    }
    if (existingIndex < 0) {
        for (int index = 0; index < profiles_.size(); ++index) {
            if (profiles_.at(index).id.compare(profile.id, Qt::CaseInsensitive) == 0) {
                emulatorStatus_->setText(
                    QStringLiteral("A profile with id '%1' already exists. Select it in the table "
                                   "to edit it.")
                        .arg(profile.id));
                emulatorStatus_->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
                return;
            }
        }
    }

    const QVector<EmulatorProfile> previous = profiles_;
    if (existingIndex >= 0) {
        profile.builtIn = profiles_.at(existingIndex).builtIn;
        profiles_[existingIndex] = profile;
    } else {
        const QVector<EmulatorProfile> builtIns = builtInProfiles();
        for (const EmulatorProfile& builtIn : builtIns) {
            if (builtIn.id.compare(profile.id, Qt::CaseInsensitive) == 0) {
                profile.builtIn = true;
                break;
            }
        }
        profiles_.append(profile);
    }
    if (!saveProfiles(settings_, profiles_, error)) {
        profiles_ = previous;
        emulatorStatus_->setText(QStringLiteral("Save failed: %1").arg(error));
        emulatorStatus_->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
        setStatusMessage(QStringLiteral("Emulator save failed: %1").arg(error));
        return;
    }
    editingProfileId_ = profile.id;
    populateEmulatorTable(profile.id);
    reinstallDefaultEmulatorCombos();
    updateImportProfileCombos();
    updateHeaderStatus();
    emulatorStatus_->setStyleSheet(QString());
    emulatorStatus_->setText(QStringLiteral("Saved '%1' to emulators.json.").arg(profile.id));
    setStatusMessage(QStringLiteral("Saved emulator profile '%1'.").arg(profile.id));
}

void MainWindow::removeEmulatorProfile() {
    if (isBusy())
        return;
    const int index = selectedEmulatorIndex();
    if (index < 0 || index >= profiles_.size())
        return;
    const EmulatorProfile profile = profiles_.at(index);
    if (profile.builtIn) {
        emulatorStatus_->setText(
            QStringLiteral("Built-in profiles cannot be removed. Clear the executable instead."));
        emulatorStatus_->setStyleSheet(QStringLiteral("QLabel { color: #e8bd6a; }"));
        return;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Remove emulator profile"),
        QStringLiteral("Remove the user profile '%1' from emulators.json?\n\nPackages that "
                       "reference it will need a new emulator set in Edit.")
            .arg(profile.name.isEmpty() ? profile.id : profile.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    const QVector<EmulatorProfile> previous = profiles_;
    profiles_.removeAt(index);
    QString error;
    if (!saveProfiles(settings_, profiles_, error)) {
        profiles_ = previous;
        emulatorStatus_->setText(QStringLiteral("Remove failed: %1").arg(error));
        emulatorStatus_->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
        return;
    }
    editingProfileId_.clear();
    clearEmulatorForm();
    populateEmulatorTable();
    reinstallDefaultEmulatorCombos();
    updateImportProfileCombos();
    updateHeaderStatus();
    emulatorStatus_->setText(QStringLiteral("Removed '%1'.").arg(profile.id));
    emulatorStatus_->setStyleSheet(QString());
}

void MainWindow::runDetection(bool persist) {
    if (detectionRunning_) {
        setStatusMessage(QStringLiteral("Emulator detection is already running."));
        return;
    }
    if (isBusy()) {
        setStatusMessage(QStringLiteral("Wait for the current scan or import to finish."));
        return;
    }
    detectionRunning_ = true;
    detectorLabel_->setText(QStringLiteral("Detecting installed emulators..."));
    if (emulatorEvidence_)
        emulatorEvidence_->setPlainText(QStringLiteral("Detection running..."));
    updateBusyUi();
    setStatusMessage(QStringLiteral("Detecting installed emulators..."));
    detectionThread_ = QThread::create([this, persist] {
        QStringList notes;
        QVector<EmulatorProfile> detected = detectInstalled(&notes);
        const QHash<QString, QString> portable =
            findPortableEmulatorExecutables(detected, &notes);
        for (EmulatorProfile& profile : detected) {
            if (!profile.executable.trimmed().isEmpty())
                continue;
            const auto found = portable.constFind(profile.id);
            if (found == portable.constEnd())
                continue;
            profile.executable = found.value();
            profile.workingDirectory = QFileInfo(found.value()).absolutePath();
        }
        if (!portable.isEmpty()) {
            QStringList filtered;
            for (const QString& note : notes) {
                bool contradicts = false;
                for (const QString& id : portable.keys()) {
                    if (note.compare(id + QStringLiteral(": not detected"),
                                     Qt::CaseInsensitive) == 0) {
                        contradicts = true;
                        break;
                    }
                }
                if (!contradicts)
                    filtered.append(note);
            }
            QStringList positive;
            QStringList negative;
            for (const QString& note : filtered) {
                if (note.contains(QStringLiteral("not detected")))
                    negative.append(note);
                else
                    positive.append(note);
            }
            notes = positive + negative;
        }
        QMetaObject::invokeMethod(
            this, [this, detected, notes, persist] { applyDetection(detected, notes, persist); },
            Qt::QueuedConnection);
    });
    connect(detectionThread_.data(), &QThread::finished, detectionThread_.data(),
            &QObject::deleteLater);
    detectionThread_->start();
}

void MainWindow::applyDetection(const QVector<EmulatorProfile>& detected,
                                const QStringList& notes, bool persist) {
    detectionRunning_ = false;
    detectionNotes_ = notes.join(QLatin1Char('\n'));
    if (emulatorEvidence_)
        emulatorEvidence_->setPlainText(detectionNotes_);
    int detectedCount = 0;
    int adopted = 0;
    bool changed = false;
    QStringList adoptedNames;
    for (const EmulatorProfile& candidate : detected) {
        if (candidate.executable.trimmed().isEmpty())
            continue;
        ++detectedCount;
        EmulatorProfile* existing = findProfile(profiles_, candidate.id);
        if (!existing) {
            profiles_.append(candidate);
            ++adopted;
            changed = true;
            adoptedNames.append(candidate.name.isEmpty() ? candidate.id : candidate.name);
            continue;
        }
        const bool missing =
            existing->executable.trimmed().isEmpty() || !QFileInfo(existing->executable).isFile();
        if (missing) {
            existing->executable = candidate.executable;
            if (existing->workingDirectory.trimmed().isEmpty())
                existing->workingDirectory = candidate.workingDirectory;
            ++adopted;
            changed = true;
            adoptedNames.append(existing->name.isEmpty() ? existing->id : existing->name);
        }
    }

    QString saveError;
    if (persist && changed && !saveProfiles(settings_, profiles_, saveError)) {
        setHeaderNotice(QStringLiteral("Detected emulators were applied in memory but not saved: "
                                       "%1")
                            .arg(saveError));
        appendImportReport(QStringLiteral("Detection save failed: %1").arg(saveError));
    } else if (persist && !changed) {
        appendImportReport(QStringLiteral("Detection: no profile changes to save."));
    }

    populateEmulatorTable(editingProfileId_);
    reinstallDefaultEmulatorCombos();
    updateImportProfileCombos();
    updateHeaderStatus();
    refreshDiagnostics();
    updateBusyUi();

    if (detectedCount == 0) {
        setStatusMessage(
            QStringLiteral("No installed emulators detected. See the evidence lines."));
        if (emulatorStatus_) {
            emulatorStatus_->setText(
                QStringLiteral("No installed emulators detected; set an executable manually."));
            emulatorStatus_->setStyleSheet(QStringLiteral("QLabel { color: #e8bd6a; }"));
        }
    } else {
        const QString names = adoptedNames.isEmpty()
            ? QStringLiteral("no profile changes")
            : QStringLiteral("updated %1 profile(s): %2")
                  .arg(adopted)
                  .arg(adoptedNames.join(QStringLiteral(", ")));
        const QString persistence =
            persist ? (changed ? QStringLiteral(" Saved to emulators.json.")
                               : QStringLiteral(" No profile changes to save."))
                    : QStringLiteral(" Applied for this session; press Detect installed to save.");
        const QString message =
            QStringLiteral("Detected %1 emulator(s); %2.%3").arg(detectedCount).arg(names).arg(
                persistence);
        setStatusMessage(message);
        if (emulatorStatus_) {
            emulatorStatus_->setText(message);
            emulatorStatus_->setStyleSheet(QString());
        }
    }
}

void MainWindow::updateArgumentPreview() {
    if (!emulatorPreview_)
        return;
    EmulatorProfile profile;
    profile.id = emulatorIdEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("emulator")
        : emulatorIdEdit_->text().trimmed();
    profile.argumentTemplate = emulatorTemplateEdit_->text();
    for (const QString& part : emulatorPlatformsEdit_->text().split(QLatin1Char(','))) {
        const QString platform = part.trimmed();
        if (!platform.isEmpty() && !profile.platforms.contains(platform))
            profile.platforms.append(platform);
    }
    QString sample = emulatorSampleRomEdit_->text().trimmed();
    if (sample.isEmpty()) {
        emulatorPreview_->clear();
        emulatorPreviewError_->setText(QStringLiteral("Enter a sample ROM path to preview argv."));
        emulatorPreviewError_->setVisible(true);
        return;
    }
    QString platform = profile.platforms.value(0);
    if (platform.isEmpty())
        platform = platformForPath(sample);
    QString appId = packageIdFor(sample, platform);
    const QString title = titleFromFileName(QFileInfo(sample).fileName());
    QString error;
    const QStringList arguments = expandArguments(
        profile, argumentKeyValues(profile, sample, title, appId), error);
    if (!error.isEmpty()) {
        emulatorPreview_->clear();
        emulatorPreviewError_->setText(QStringLiteral("Argument error: %1").arg(error));
        emulatorPreviewError_->setVisible(true);
        return;
    }
    emulatorPreviewError_->setVisible(false);
    emulatorPreview_->setPlainText(formatArguments(arguments));
}

QStringList MainWindow::argumentKeyValues(const EmulatorProfile& profile,
                                          const QString& romPath, const QString& title,
                                          const QString& appId, const QString& platformValue) const {
    QString platform = platformValue;
    if (platform.isEmpty())
        platform = profile.platforms.value(0);
    if (platform.isEmpty())
        platform = platformForPath(romPath);
    return QStringList{
        QStringLiteral("rom=%1").arg(romPath),
        QStringLiteral("title=%1").arg(title),
        QStringLiteral("id=%1").arg(appId),
        QStringLiteral("dir=%1").arg(QFileInfo(romPath).absolutePath()),
        QStringLiteral("core=%1").arg(defaultCoreFor(profile.id, platform)),
    };
}

QString MainWindow::sampleRomPath() const {
    return emulatorSampleRomEdit_ ? emulatorSampleRomEdit_->text().trimmed() : QString();
}

void MainWindow::testLaunchEmulator() {
    EmulatorProfile profile;
    QString error;
    if (!readEmulatorForm(profile, error)) {
        emulatorPreviewError_->setText(error);
        emulatorPreviewError_->setVisible(true);
        return;
    }
    const QString sample = sampleRomPath();
    if (sample.isEmpty()) {
        emulatorPreviewError_->setText(QStringLiteral("Enter a sample ROM path first."));
        emulatorPreviewError_->setVisible(true);
        return;
    }
    if (profile.executable.trimmed().isEmpty()) {
        emulatorPreviewError_->setText(
            QStringLiteral("Set the emulator executable before test launching."));
        emulatorPreviewError_->setVisible(true);
        return;
    }
    const QString title = titleFromFileName(QFileInfo(sample).fileName());
    const QString appId = packageIdFor(sample, profile.platforms.value(0));
    QStringList arguments = expandArguments(
        profile, argumentKeyValues(profile, sample, title, appId), error);
    if (!error.isEmpty()) {
        emulatorPreviewError_->setText(QStringLiteral("Argument error: %1").arg(error));
        emulatorPreviewError_->setVisible(true);
        return;
    }
    emulatorPreviewError_->setVisible(false);
    setUiValue(QStringLiteral("emulators/sampleRom"), emulatorSampleRomEdit_->text());
    const QString displayName =
        profile.name.isEmpty() ? profile.id : profile.name;
    runProgram(profile.executable, arguments, profile.workingDirectory, profile.env,
               displayName, emulatorLaunchResult_);
}

void MainWindow::browseForExecutable() {
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose the emulator executable"), lastBrowseDirectory_,
        QStringLiteral("Executables (*.exe);;All files (*)"));
    if (file.isEmpty())
        return;
    emulatorExecutableEdit_->setText(QDir::toNativeSeparators(file));
    if (emulatorWorkingDirectoryEdit_->text().trimmed().isEmpty())
        emulatorWorkingDirectoryEdit_->setText(
            QDir::toNativeSeparators(QFileInfo(file).absolutePath()));
    lastBrowseDirectory_ = QFileInfo(file).absolutePath();
    setUiValue(QStringLiteral("paths/lastBrowse"), lastBrowseDirectory_);
    updateArgumentPreview();
}

void MainWindow::runProgram(const QString& program, const QStringList& arguments,
                            const QString& workingDirectory,
                            const QHash<QString, QString>& environment,
                            const QString& displayName, QLabel* resultLabel) {
    if (!resultLabel)
        return;
    if (program.trimmed().isEmpty() || !QFileInfo(program).isFile()) {
        resultLabel->setText(
            QStringLiteral("Error: executable not found: %1").arg(
                QDir::toNativeSeparators(program)));
        resultLabel->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
        setStatusMessage(resultLabel->text());
        return;
    }
    auto* process = new QProcess(this);
    QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it)
        processEnvironment.insert(it.key(), it.value());
    process->setProcessEnvironment(processEnvironment);
    process->setProgram(program);
    process->setArguments(arguments);
    if (!workingDirectory.trimmed().isEmpty() && QFileInfo(workingDirectory).isDir())
        process->setWorkingDirectory(workingDirectory);
    launches_.append(process);
    resultLabel->setStyleSheet(QStringLiteral("QLabel { color: #a9bed6; }"));
    resultLabel->setText(QStringLiteral("Launching %1...").arg(displayName));
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();

    connect(process, &QProcess::started, this, [resultLabel, displayName, process] {
        resultLabel->setText(
            QStringLiteral("%1 started (PID %2).").arg(displayName).arg(process->processId()));
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, resultLabel, displayName, process](QProcess::ProcessError processError) {
                if (processError != QProcess::FailedToStart)
                    return;
                const QString message =
                    QStringLiteral("Could not start %1: %2")
                        .arg(displayName, process->errorString());
                resultLabel->setText(message);
                resultLabel->setStyleSheet(QStringLiteral("QLabel { color: #ef9a9a; }"));
                setStatusMessage(message);
                launches_.removeAll(process);
                process->deleteLater();
            });
    connect(process, &QProcess::finished, this,
            [this, resultLabel, displayName, process, startedAt](int exitCode,
                                                                 QProcess::ExitStatus status) {
                const double seconds =
                    double(QDateTime::currentMSecsSinceEpoch() - startedAt) / 1000.0;
                QString message;
                if (status == QProcess::CrashExit)
                    message = QStringLiteral("%1 crashed after %2 s.")
                                  .arg(displayName)
                                  .arg(seconds, 0, 'f', 1);
                else
                    message = QStringLiteral("%1 exited with code %2 after %3 s.")
                                  .arg(displayName)
                                  .arg(exitCode)
                                  .arg(seconds, 0, 'f', 1);
                resultLabel->setText(message);
                resultLabel->setStyleSheet(
                    QStringLiteral("QLabel { color: %1; }")
                        .arg(status == QProcess::CrashExit || exitCode != 0 ? QStringLiteral("#e8bd6a")
                                                                           : QStringLiteral("#8fd18f")));
                setStatusMessage(message);
                launches_.removeAll(process);
                process->deleteLater();
            });
    process->start();
}

void MainWindow::detachRunningProcesses() {
    for (QProcess* process : launches_) {
        if (!process)
            continue;
        if (process->state() != QProcess::NotRunning) {
            process->disconnect();
            process->setParent(nullptr);
        }
    }
    launches_.clear();
}

QString MainWindow::buildDiagnosticsReport(int* problemCount) const {
    int problems = 0;
    QStringList lines;
    const auto problem = [&](const QString& text) {
        lines.append(QStringLiteral("PROBLEM: ") + text);
        ++problems;
    };

    lines.append(QStringLiteral("XMB Manager diagnostics"));
    lines.append(QStringLiteral("Generated: %1").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    lines.append(QStringLiteral("Executable folder: %1")
                     .arg(QDir::toNativeSeparators(settings_.executableDirectory)));
    lines.append(QStringLiteral("Settings file: %1")
                     .arg(QDir::toNativeSeparators(settings_.iniPath)));
    lines.append(QStringLiteral("Data folder: %1")
                     .arg(QDir::toNativeSeparators(settings_.dataDirectory)));
    lines.append(QStringLiteral("Report path: %1 (schema xmb-manager-report/2)")
                     .arg(QDir::toNativeSeparators(reportPath())));
    lines.append(QStringLiteral("  The CLI writes that JSON with --export-report. The manager core "
                                "exposes no report writer, so this UI shows the path and the "
                                "schema instead of fabricating the document."));
    lines.append(QString());

    lines.append(QStringLiteral("Data layout"));
    const QString appsPath = registryPath(settings_);
    const QString pkgPath = packageRoot(settings_);
    const QString emulatorsFile = emulatorsPath(settings_);
    const QString collectionsFile = collectionsPath(settings_);
    const auto describeFile = [](const QString& path) {
        const QFileInfo info(path);
        if (!info.exists())
            return QStringLiteral("missing");
        return QStringLiteral("%1 bytes").arg(info.size());
    };
    lines.append(QStringLiteral("  apps.json: %1").arg(describeFile(appsPath)));
    lines.append(QStringLiteral("  pkg/: %1").arg(pkgPath));
    lines.append(QStringLiteral("  emulators.json: %1").arg(describeFile(emulatorsFile)));
    lines.append(QStringLiteral("  collections.json: %1").arg(describeFile(collectionsFile)));
    lines.append(QStringLiteral("  manager.ini: %1").arg(describeFile(settings_.iniPath)));
    lines.append(QStringLiteral("  manager-ui.ini: %1")
                     .arg(describeFile(QDir(settings_.executableDirectory)
                                           .filePath(QStringLiteral("manager-ui.ini")))));
    if (!QFileInfo(settings_.dataDirectory).isDir())
        problem(QStringLiteral("Data folder does not exist: %1").arg(settings_.dataDirectory));
    else if (!QFileInfo(settings_.dataDirectory).isWritable())
        problem(QStringLiteral("Data folder is not writable: %1").arg(settings_.dataDirectory));
    if (!QFileInfo(pkgPath).isDir())
        problem(QStringLiteral("Package root is missing: %1").arg(pkgPath));
    lines.append(QString());

    lines.append(QStringLiteral("Registry and package validation"));
    Settings settingsCopy = settings_;
    QString listError;
    const QVector<PackageRecord> records = listPackages(settingsCopy, listError);
    const bool listed = listError.isEmpty();
    if (!listed)
        problem(QStringLiteral("listPackages failed: %1").arg(listError));
    lines.append(QStringLiteral("  package records: %1").arg(records.size()));
    QSet<QString> knownIds;
    for (const PackageRecord& record : records) {
        knownIds.insert(record.id);
        LibraryRow row;
        row.missingPackage = record.missingPackage;
        row.orphaned = record.orphaned;
        row.issue = record.issue;
        row.packagePresent = record.packagePresent;
        row.registryPresent = record.registryPresent;
        const QString state = libraryStateText(row);
        lines.append(QStringLiteral("  [%1] %2 (%3)")
                         .arg(state, record.title.isEmpty() ? record.id : record.title,
                              record.id));
        if (record.orphaned)
            problem(QStringLiteral("Orphan package '%1': pkg.json exists without an apps.json "
                                   "entry")
                        .arg(record.id));
        if (record.missingPackage)
            problem(QStringLiteral("Registry-only entry '%1': no pkg.json in %2")
                        .arg(record.id, record.packagePath));
        if (!record.issue.trimmed().isEmpty() && !record.orphaned && !record.missingPackage)
            problem(QStringLiteral("Package '%1': %2").arg(record.id, record.issue));
        if (!record.romPath.isEmpty() && !QFileInfo::exists(record.romPath))
            problem(QStringLiteral("ROM is missing for '%1': %2")
                        .arg(record.id, record.romPath));
        if (!record.art.isEmpty() && !QFileInfo::exists(record.art))
            problem(QStringLiteral("Artwork is missing for '%1': %2")
                        .arg(record.id, record.art));
        if (!record.emulatorId.isEmpty()) {
            const EmulatorProfile* profile = findProfile(profiles_, record.emulatorId);
            if (!profile)
                problem(QStringLiteral("Package '%1' references unknown emulator '%2'")
                            .arg(record.id, record.emulatorId));
            else if (profile->executable.trimmed().isEmpty())
                problem(QStringLiteral("Package '%1' emulator '%2' has no executable")
                            .arg(record.id, profile->id));
        }
    }
    lines.append(QString());

    lines.append(QStringLiteral("Emulator profiles"));
    int builtInCount = 0;
    int userCount = 0;
    for (const EmulatorProfile& profile : profiles_) {
        if (profile.builtIn)
            ++builtInCount;
        else
            ++userCount;
        lines.append(QStringLiteral("  %1 (%2): %3")
                         .arg(profile.name.isEmpty() ? profile.id : profile.name, profile.id,
                              profile.executable.isEmpty()
                                  ? QStringLiteral("no executable")
                                  : QDir::toNativeSeparators(profile.executable)));
        if (!profile.executable.trimmed().isEmpty()
            && !QFileInfo(profile.executable).isFile())
            problem(QStringLiteral("Emulator '%1' executable does not exist: %2")
                        .arg(profile.id, profile.executable));
    }
    lines.append(QStringLiteral("  built-in: %1, user: %2").arg(builtInCount).arg(userCount));
    if (!detectionNotes_.trimmed().isEmpty()) {
        lines.append(QStringLiteral("  last detection evidence:"));
        for (const QString& note : detectionNotes_.split(QLatin1Char('\n')))
            lines.append(QStringLiteral("    %1").arg(note));
    }
    lines.append(QString());

    lines.append(QStringLiteral("Platform map"));
    lines.append(QStringLiteral("  file: %1").arg(mappingFileState_));
    if (!mappingError_.isEmpty())
        problem(QStringLiteral("platform-map.json: %1").arg(mappingError_));
    QVector<PlatformMapping> mappingCheck;
    QString mappingLoadError;
    if (!loadPlatformMap(settings_, mappingCheck, mappingLoadError)) {
        problem(QStringLiteral("loadPlatformMap failed: %1").arg(mappingLoadError));
    } else {
        QStringList gaps;
        int mapped = 0;
        int fallback = 0;
        for (const QString& platform : mappingPlatforms()) {
            Settings resolveSettings = settings_;
            QString resolvedId;
            QStringList resolvedOverride;
            bool fromMap = false;
            QString reason;
            const bool resolved = resolvePlatformTarget(resolveSettings, platform, resolvedId,
                                                        resolvedOverride, fromMap, reason);
            const QString source =
                resolved ? mappingSourceText(fromMap, !resolvedId.isEmpty())
                         : QStringLiteral("error");
            if (source == QStringLiteral("map"))
                ++mapped;
            else if (source == QStringLiteral("profile fallback"))
                ++fallback;
            else if (source == QStringLiteral("unmapped")) {
                gaps.append(platform);
            } else {
                problem(QStringLiteral("Platform '%1' could not be resolved: %2")
                            .arg(platform, reason));
            }
            lines.append(QStringLiteral("  %1 -> %2 | %3 | %4")
                             .arg(platform,
                                  resolvedId.isEmpty() ? QStringLiteral("(none)") : resolvedId,
                                  source,
                                  resolvedOverride.isEmpty()
                                      ? QStringLiteral("(profile template)")
                                      : formatArguments(resolvedOverride)));
            if (const PlatformMapping* entry = findPlatformMapping(mappingCheck, platform)) {
                if (!entry->emulatorId.isEmpty() && !findProfile(profiles_, entry->emulatorId))
                    problem(QStringLiteral("platform-map.json maps '%1' to unknown emulator '%2'")
                                .arg(platform, entry->emulatorId));
            }
        }
        lines.append(QStringLiteral("  mapped: %1, profile fallback: %2, gaps: %3")
                         .arg(mapped)
                         .arg(fallback)
                         .arg(gaps.size()));
        if (!gaps.isEmpty()) {
            lines.append(QStringLiteral("  mapping gap(s), not an error but imports fail with the "
                                        "core's reason: %1")
                             .arg(gaps.join(QStringLiteral(", "))));
        }
    }
    lines.append(QString());

    lines.append(QStringLiteral("Collections"));
    QHash<QString, QStringList> collections;
    QString collectionsError;
    if (!readCollections(settings_, collections, collectionsError)) {
        problem(QStringLiteral("collections.json: %1").arg(collectionsError));
    } else if (collections.isEmpty()) {
        lines.append(QStringLiteral("  (none)"));
    } else {
        for (auto it = collections.constBegin(); it != collections.constEnd(); ++it) {
            lines.append(QStringLiteral("  %1: %2 id(s)").arg(it.key()).arg(it.value().size()));
            for (const QString& id : it.value()) {
                if (!knownIds.contains(id))
                    problem(QStringLiteral("Collection '%1' references unknown package '%2'")
                                .arg(it.key(), id));
            }
        }
    }
    // pkg.json "collection" and collections.json membership must agree in both directions.
    QHash<QString, QString> packageCollection;
    for (const PackageRecord& record : records)
        packageCollection.insert(record.id, record.collection);
    for (const PackageRecord& record : records) {
        if (record.collection.trimmed().isEmpty())
            continue;
        if (!collections.contains(record.collection)) {
            problem(QStringLiteral("Package '%1' records collection '%2' but collections.json has "
                                   "no such collection")
                        .arg(record.id, record.collection));
        } else if (!collections.value(record.collection).contains(record.id)) {
            problem(QStringLiteral("Package '%1' records collection '%2' but collections.json does "
                                   "not list it as a member")
                        .arg(record.id, record.collection));
        }
    }
    for (auto it = collections.constBegin(); it != collections.constEnd(); ++it) {
        for (const QString& id : it.value()) {
            const auto found = packageCollection.constFind(id);
            if (found == packageCollection.constEnd())
                continue;
            if (found.value().compare(it.key(), Qt::CaseInsensitive) != 0) {
                problem(QStringLiteral("collections.json lists '%1' under '%2' but pkg.json says "
                                       "'%3'")
                            .arg(id, it.key(),
                                 found.value().isEmpty() ? QStringLiteral("(none)")
                                                         : found.value()));
            }
        }
    }
    lines.append(QString());

    lines.append(QStringLiteral("Tags"));
    QHash<QString, QStringList> tagIds;
    for (const PackageRecord& record : records) {
        for (const QString& tag : record.tags) {
            QString tagError;
            if (!isValidTag(tag, tagError))
                problem(QStringLiteral("Package '%1' tag: %2").arg(record.id, tagError));
            tagIds[tag].append(record.id);
        }
    }
    if (tagIds.isEmpty()) {
        lines.append(QStringLiteral("  (none)"));
    } else {
        QStringList tagNames = tagIds.keys();
        tagNames.sort(Qt::CaseInsensitive);
        for (const QString& tag : tagNames) {
            lines.append(QStringLiteral("  %1: %2 package(s)").arg(tag).arg(tagIds.value(tag).size()));
        }
    }
    lines.append(QString());

    lines.append(QStringLiteral("Hash verification (this session)"));
    if (lastHashChecks_.isEmpty()) {
        lines.append(QStringLiteral("  not run yet; use Verify hashes (Ctrl+Shift+H)"));
    } else {
        lines.append(QStringLiteral("  last run: %1").arg(lastHashCheckTime_));
        int okCount = 0;
        for (const HashVerification& check : lastHashChecks_) {
            if (check.state == QStringLiteral("ok"))
                ++okCount;
            lines.append(QStringLiteral("  %1 | %2 | %3 | %4")
                             .arg(check.state, check.id,
                                  check.storedLabel.isEmpty() ? QStringLiteral("(no hash)")
                                                              : check.storedLabel,
                                  check.error));
            if (check.state != QStringLiteral("ok")) {
                problem(QStringLiteral("Hash %1 for '%2': %3")
                            .arg(check.state, check.id,
                                 check.error.isEmpty() ? QStringLiteral("(no detail)")
                                                       : check.error));
            }
        }
        lines.append(QStringLiteral("  %1 checked, %2 ok, %3 not ok")
                         .arg(lastHashChecks_.size())
                         .arg(okCount)
                         .arg(lastHashChecks_.size() - okCount));
    }
    lines.append(QString());

    QVector<ContentApp> apps;
    QString registryError;
    if (!ContentRegistry::load(appsPath, apps, registryError)) {
        problem(QStringLiteral("apps.json: %1").arg(registryError));
    } else {
        lines.append(QStringLiteral("apps.json entries: %1").arg(apps.size()));
        for (const ContentApp& app : apps) {
            const bool managerOwned = app.id.startsWith(QStringLiteral("pkg-"));
            if (managerOwned && !knownIds.contains(app.id))
                problem(QStringLiteral("apps.json entry '%1' has no package record").arg(app.id));
            if (managerOwned && app.executable.isEmpty())
                problem(QStringLiteral("apps.json entry '%1' has no executable").arg(app.id));
        }
    }
    lines.append(QString());

    lines.append(QStringLiteral("Last import report (this session)"));
    const QString importText =
        importReport_ ? importReport_->toPlainText().trimmed() : QString();
    if (importText.isEmpty())
        lines.append(QStringLiteral("  (none)"));
    else
        lines.append(importText);
    lines.append(QString());

    lines.append(QStringLiteral("CLI/JSON report"));
    const QStringList candidates = {
        QStringLiteral("xmb-manager-report-2.json"), QStringLiteral("import-report.json"),
        QStringLiteral("last-import.json"), QStringLiteral("manager-report.json"),
        QStringLiteral("xmb-import-report.json"), QStringLiteral("report.json")};
    bool foundReport = false;
    for (const QString& name : candidates) {
        const QString path = QDir(settings_.dataDirectory).filePath(name);
        const QFileInfo info(path);
        if (!info.isFile())
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            problem(QStringLiteral("Could not read '%1': %2").arg(path, file.errorString()));
            continue;
        }
        const QByteArray bytes = file.read(2 * 1024 * 1024);
        lines.append(QStringLiteral("  %1:").arg(path));
        lines.append(QString::fromUtf8(bytes));
        foundReport = true;
        break;
    }
    if (!foundReport)
        lines.append(QStringLiteral("  no CLI/JSON report file found in the data folder. The CLI "
                                    "writes its report to the path given on its command line."));
    if (!startupErrors_.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("Startup warnings"));
        for (const QString& warning : startupErrors_)
            lines.append(QStringLiteral("  %1").arg(warning));
    }

    if (problemCount)
        *problemCount = problems;
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::copyDiagnostics() {
    const QString report = lastDiagnosticsReport_.isEmpty()
        ? buildDiagnosticsReport(nullptr)
        : lastDiagnosticsReport_;
    QApplication::clipboard()->setText(report);
    setStatusMessage(QStringLiteral("Diagnostics report copied to the clipboard."));
}

void MainWindow::reinstallDefaultEmulatorCombos() {
    if (!defaultEmulatorForm_)
        return;
    while (defaultEmulatorForm_->count() > 0) {
        QLayoutItem* item = defaultEmulatorForm_->takeAt(0);
        if (!item)
            break;
        if (QWidget* widget = item->widget())
            delete widget;
        delete item;
    }
    defaultEmulatorCombos_.clear();

    QSet<QString> platformSet;
    for (const EmulatorProfile& profile : profiles_) {
        for (const QString& platform : profile.platforms)
            platformSet.insert(platform);
    }
    for (auto it = defaultEmulators_.constBegin(); it != defaultEmulators_.constEnd(); ++it)
        platformSet.insert(it.key());
    QStringList platforms;
    for (const QString& platform : platformSet)
        platforms.append(platform);
    std::sort(platforms.begin(), platforms.end(), [](const QString& left, const QString& right) {
        return QString::compare(left, right, Qt::CaseInsensitive) < 0;
    });

    if (platforms.isEmpty()) {
        defaultEmulatorForm_->addRow(
            makeMutedLabel(defaultEmulatorHost_,
                           QStringLiteral("No emulator profiles or platforms are available yet.")));
        return;
    }
    for (const QString& platform : platforms) {
        auto* combo = new QComboBox(defaultEmulatorHost_);
        combo->addItem(QStringLiteral("(use platform match)"), QString());
        for (const EmulatorProfile& profile : profiles_)
            combo->addItem(profile.name.isEmpty() ? profile.id : profile.name, profile.id);
        const QString stored = storedDefaultEmulator(platform);
        int index = stored.isEmpty() ? 0 : combo->findData(stored);
        if (index < 0)
            index = 0;
        combo->setCurrentIndex(index);
        combo->setMaximumWidth(380);
        combo->setToolTip(
            QStringLiteral("Preselected for %1 imports; profiles are stored in emulators.json")
                .arg(platform));
        connect(combo, &QComboBox::currentIndexChanged, this, [this, platform, combo](int) {
            setDefaultEmulatorForPlatform(platform, combo->currentData().toString());
        });
        auto* label = new QLabel(platform, defaultEmulatorHost_);
        label->setObjectName(QStringLiteral("muted"));
        defaultEmulatorForm_->addRow(label, combo);
        defaultEmulatorCombos_.insert(platform, combo);
    }
}

void MainWindow::setDefaultEmulatorForPlatform(const QString& platform,
                                               const QString& emulatorId) {
    if (platform.isEmpty())
        return;
    const QString key = QStringLiteral("defaultEmulator/%1").arg(prefPlatformKey(platform));
    if (emulatorId.isEmpty()) {
        defaultEmulators_.remove(platform);
        if (uiSettings_)
            uiSettings_->remove(key);
    } else {
        defaultEmulators_.insert(platform, emulatorId);
        setUiValue(key, emulatorId);
    }
    const QString effective = emulatorId.isEmpty() ? emulatorForPlatform(platform) : emulatorId;
    for (ImportRow& row : importRows_) {
        if (row.game.platform.compare(platform, Qt::CaseInsensitive) == 0) {
            row.emulatorId = effective;
            if (!emulatorId.isEmpty())
                row.emulatorUserChosen = true;
        }
    }
    updateImportProfileCombos();
}

void MainWindow::applySettingsDataDirectory() {
    const QString directory = settingsDataDirectoryEdit_->text().trimmed();
    if (directory.isEmpty()) {
        settingsMessage_->setText(QStringLiteral("Enter an absolute data folder path."));
        settingsMessage_->setVisible(true);
        return;
    }
    if (QDir::cleanPath(QDir::fromNativeSeparators(directory))
            .compare(QDir::cleanPath(settings_.dataDirectory), Qt::CaseInsensitive)
        == 0) {
        settingsMessage_->setVisible(false);
        setStatusMessage(QStringLiteral("That is already the current data folder."));
        return;
    }
    if (changeDataDirectory(directory))
        settingsMessage_->setVisible(false);
}

void MainWindow::resetSettingsDefaults() {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Reset to defaults"),
        QStringLiteral("Reset XMB Manager UI preferences?\n\nDefault emulators, the collection "
                       "field, the sample ROM path, and window layout are cleared. Packages, "
                       "registrations, and emulator profiles are not deleted."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    if (uiSettings_) {
        const QStringList keys = uiSettings_->allKeys();
        for (const QString& key : keys) {
            if (key.startsWith(QStringLiteral("defaultEmulator/"))
                || key.startsWith(QStringLiteral("import/"))
                || key.startsWith(QStringLiteral("emulators/"))
                || key.startsWith(QStringLiteral("window/")))
                uiSettings_->remove(key);
        }
        uiSettings_->sync();
    }
    defaultEmulators_.clear();
    if (importCollectionEdit_)
        importCollectionEdit_->clear();
    if (emulatorSampleRomEdit_)
        emulatorSampleRomEdit_->setText(QStringLiteral("C:/Games/Sample Game.iso"));
    for (ImportRow& row : importRows_)
        row.emulatorId = emulatorForPlatform(row.game.platform);
    reinstallDefaultEmulatorCombos();
    updateImportProfileCombos();
    setStatusMessage(
        QStringLiteral("UI preferences reset. Packages and emulator profiles were not touched."));
}

} // namespace xmb::manager
