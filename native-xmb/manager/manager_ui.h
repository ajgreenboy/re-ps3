#pragma once

// XMB Manager UI: the standalone Qt Widgets front end for the manager core.
//
// The window owns no packaging rules. Every mutation goes through the core:
// createPackage/removePackage/listPackages, loadProfiles/saveProfiles,
// loadPlatformMap/savePlatformMap, updatePackageTags, setPackageCollection,
// renameCollection/removeCollection, applyBulk, relinkPackage,
// verifyPackageHashes, packageArgv/launchPackage, writeBackup/restoreBackup,
// expandArguments, scanPaths, importRealPkg, and the Settings helpers in
// manager_core.h. The UI only collects input, runs slow core calls off the GUI
// thread, and reports the core's exact messages.

#include "manager_core.h"
#include "manager_emulators.h"
#include "manager_package.h"
#include "manager_scanner.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <atomic>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QFormLayout;
class QFrame;
class QHBoxLayout;
class QJsonArray;
class QJsonObject;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QSettings;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QThread;
class QTreeWidget;
class QWidget;

namespace xmb::manager {

// One row of the library table, flattened from PackageRecord plus the ROM size
// and the optional record extensions (tags, hash, mtime).
struct LibraryRow {
    QString id;
    QString title;
    QString platform;
    QString emulatorId;
    QString collection;
    QString romPath;
    QString romKind;
    QStringList arguments;
    QString artPath;
    QString packagePath;
    QString issue;
    QString createdAt;
    bool packagePresent = false;
    bool registryPresent = false;
    bool orphaned = false;
    bool missingPackage = false;
    qint64 size = 0;
    // Extensions recorded by the core's package writer.
    QStringList tags;
    QString hashLabel;
    QString hashValue;
    QString romMtime;
    qint64 recordedSize = 0;
};

// One row of the import table.
struct ImportRow {
    DetectedGame game;
    QString emulatorId;
    QString result;
    int resultKind = 0; // 0 = pending, 1 = imported, 2 = duplicate refused, 3 = error
    bool selected = true;
    // True once the user picked the emulator for this row. When it is false the
    // import resolves the platform through the core's platform map first.
    bool emulatorUserChosen = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Settings settings, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Construction.
    void applyTheme();
    void buildUi();
    void buildMenus();
    QWidget* buildHeader();
    QWidget* buildLibraryTab();
    QWidget* buildImportTab();
    QWidget* buildEmulatorsTab();
    QWidget* buildPlatformMapTab();
    QWidget* buildDiagnosticsTab();
    QWidget* buildSettingsTab();
    void buildStatusBar();
    void connectShortcuts();
    void restoreUiState();
    void saveUiState();
    void loadUiPreferences();

    // Shared state.
    bool isBusy() const;
    void updateBusyUi();
    void updateHeaderStatus();
    void ensureLayoutAndRefresh();
    void refreshAll(bool runDetection);
    bool refreshProfiles();
    void refreshLibrary();
    void refreshDiagnostics();
    void updateImportProfileCombos();
    void setHeaderNotice(const QString& message);
    void setStatusMessage(const QString& message, int timeout = 7000);
    void appendImportReport(const QString& line);
    QVariant uiValue(const QString& key, const QVariant& fallback = QVariant()) const;
    void setUiValue(const QString& key, const QVariant& value);
    QString defaultCollection() const;
    QString xmbDataDirectory() const;
    QString lastBrowseDirectory() const;

    // Background library tasks. One runs at a time and every one of them is a
    // slow core call: bulk writes, hash verification, relink, launchPackage,
    // writeBackup and restoreBackup.
    bool beginTask(const QString& name);
    void endTask();
    void waitForTasks();

    // Data directory.
    void browseDataDirectory();
    bool changeDataDirectory(const QString& directory);
    void openDataFolder();
    void useDetectedXmbDataDirectory();

    // Library tab: filtering, grouping and selection.
    bool libraryRowMatches(const LibraryRow& row) const;
    void applyLibraryFilter();
    void rebuildLibraryTable(const QStringList& keepIds);
    void rebuildTagFilter();
    void setTagFilter(const QString& tag, bool enabled);
    void clearLibraryFilters();
    void updateLibrarySelection();
    void updateLibraryActions();
    void updateBulkBar();
    int selectedLibraryIndex() const;
    QVector<int> selectedLibraryIndices() const;
    QStringList selectedLibraryIds() const;
    bool libraryIndexForTableRow(int tableRow, int& index) const;
    QString libraryStateText(const LibraryRow& row) const;
    QStringList extraArgumentsFor(const LibraryRow& row, bool* matchesBase = nullptr) const;
    void showLibraryContextMenu(const QPoint& position);

    // Library tab: single-package actions.
    void launchLibraryEntry();
    void showArgvForSelected();
    void copyArgv();
    void editLibraryEntry();
    void editLibraryTags();
    void removeLibraryEntry();
    void reimportLibraryEntry();
    void relinkSelectedPackage();
    void verifySelectedHashes();
    void updateDetailHash(const LibraryRow& row);

    // Library tab: collections.
    void refreshCollections();
    void rebuildCollectionsList();
    void onCollectionFilterChanged();
    void createCollection();
    void renameSelectedCollection();
    void removeSelectedCollection();
    void assignSelectedToCollection();

    // Library tab: bulk edit.
    void bulkSetCollection();
    void bulkSetEmulator();
    void bulkAddTags();
    void bulkRemoveTags();
    void bulkRemovePackages();
    void runBulk(const BulkRequest& request, const QString& label, bool showReport = true);
    void finishBulk(bool applied, const QVector<BulkResult>& results, const QString& error,
                    const QString& label);

    // Import tab.
    void addFiles();
    void addFolder();
    void addScannedPaths(const QStringList& paths);
    void startScan(const QStringList& roots);
    void finishScan(const QVector<DetectedGame>& games, const QStringList& warnings);
    void rebuildImportTable();
    void refreshImportSelectionState();
    void importSelected();
    void importRealPkg();
    QString emulatorForPlatform(const QString& platform) const;
    QString storedDefaultEmulator(const QString& platform) const;
    void setImportProgress(int value, int maximum, const QString& text);
    void setImportBusy(bool busy);
    void startRealPkgImport(const QString& path);
    void finishImport();
    void finishRealPkgImport(bool ok, const QString& error, const QStringList& notes,
                             const QString& path);

    // Emulators tab.
    void populateEmulatorTable(const QString& selectId = QString());
    void onEmulatorSelectionChanged();
    void updateEmulatorButtons();
    void clearEmulatorForm();
    void loadEmulatorForm(const EmulatorProfile& profile);
    bool readEmulatorForm(EmulatorProfile& profile, QString& error) const;
    void addEmulatorProfile();
    void saveEmulatorProfile();
    void removeEmulatorProfile();
    void runDetection(bool persist);
    void applyDetection(const QVector<EmulatorProfile>& detected, const QStringList& notes,
                        bool persist);
    void updateArgumentPreview();
    void testLaunchEmulator();
    void browseForExecutable();
    int selectedEmulatorIndex() const;
    QString sampleRomPath() const;
    QStringList argumentKeyValues(const EmulatorProfile& profile, const QString& romPath,
                                  const QString& title, const QString& appId,
                                  const QString& platform = QString()) const;

    // Platform map tab.
    void refreshPlatformMap();
    void rebuildPlatformTable(const QString& keepPlatform);
    void onMappingSelectionChanged();
    void loadMappingEditor(const QString& platform);
    void applyMappingEditor();
    void setMappingForPlatform(const QString& platform, const QString& emulatorId,
                               const QStringList& arguments);
    void setMappingArguments(const QString& platform, const QStringList& arguments);
    void clearMappingOverride();
    void saveMappingTable();
    void revertMappingTable();
    void updateMappingState();
    bool mappingRowForTable(int tableRow, QString& platform) const;
    QStringList mappingPlatforms() const;
    QString mappingSourceFor(const QString& platform, QString* reason = nullptr) const;
    int mappingGapCount() const;
    void updateMappingBanner();
    QString mappingStatusText() const;

    // Launches (package argv through the core).
    void startLaunch(const QString& appId);
    void finishLaunch(const QString& appId, bool spawned, const LaunchOutcome& outcome,
                      const QString& error);
    void runProgram(const QString& program, const QStringList& arguments,
                    const QString& workingDirectory, const QHash<QString, QString>& environment,
                    const QString& displayName, QLabel* resultLabel);
    void detachRunningProcesses();

    // Hashes.
    void startHashVerification(const QString& focusId);
    void finishHashVerification(const QString& focusId, bool ok,
                                const QVector<HashVerification>& checks, const QString& error);
    void verifyLibraryHashes();

    // Backup and restore.
    void backupLibrary();
    void startBackup(const QString& file, const BackupOptions& options);
    void finishBackup(bool ok, const QJsonObject& summary, const QString& error,
                      const QString& file);
    void restoreLibrary();
    void startRestore(const QString& file, const QVector<PathRemap>& remaps, bool replace);
    void finishRestore(bool ok, const QJsonArray& actions, const QJsonArray& conflicts,
                       const QString& error, const QString& file,
                       const QVector<PathRemap>& remaps, bool replace);
    void showRestoreSummary(const QJsonArray& actions, const QString& file);
    QString reportPath() const;
    void copyReportPath();

    // Diagnostics tab.
    QString buildDiagnosticsReport(int* problemCount) const;
    void copyDiagnostics();

    // Settings tab.
    void applySettingsDataDirectory();
    void reinstallDefaultEmulatorCombos();
    void setDefaultEmulatorForPlatform(const QString& platform, const QString& emulatorId);
    void resetSettingsDefaults();

    // Core settings and UI-owned preferences.
    Settings settings_;
    QSettings* uiSettings_ = nullptr;
    QVector<EmulatorProfile> profiles_;
    QVector<LibraryRow> library_;
    QVector<ImportRow> importRows_;
    QHash<QString, QString> defaultEmulators_;
    QStringList startupErrors_;
    QString detectionNotes_;
    QString lastDiagnosticsReport_;
    int lastProblemCount_ = 0;
    QString lastBrowseDirectory_;

    // Platform map model (the core's effective mapping set).
    QVector<PlatformMapping> mappings_;
    QString mappingError_;
    QString mappingFileState_;
    QString mappingSelection_;
    bool mappingsDirty_ = false;
    bool mappingLoadingEditor_ = false;
    int mappingGapCount_ = 0;

    // Collections and tag filter state.
    QHash<QString, QStringList> collections_;
    QString collectionsError_;
    QString collectionFilter_;
    QStringList activeTagFilter_;
    int groupMode_ = 0; // 0 = none, 1 = collection, 2 = platform, 3 = emulator

    // Last hash verification results (session cache for the report).
    QVector<HashVerification> lastHashChecks_;
    QString lastHashCheckTime_;

    // Background work.
    QPointer<QThread> scanThread_;
    QPointer<QThread> importThread_;
    QPointer<QThread> detectionThread_;
    QPointer<QThread> taskThread_;
    std::atomic<bool> importCancel_{false};
    bool scanRunning_ = false;
    bool importRunning_ = false;
    bool detectionRunning_ = false;
    bool taskRunning_ = false;
    QString taskName_;
    bool bulkShowReport_ = true;
    bool rebuildingImportTable_ = false;
    bool loadingEmulatorForm_ = false;
    bool updatingLibraryTable_ = false;
    bool updatingTagChips_ = false;

    // Window chrome.
    QLabel* headerNotice_ = nullptr;
    QLabel* detectorLabel_ = nullptr;
    QLabel* xmbBannerLabel_ = nullptr;
    QFrame* xmbBanner_ = nullptr;
    QPushButton* useXmbButton_ = nullptr;
    QLineEdit* dataDirectoryEdit_ = nullptr;
    QPushButton* browseDataButton_ = nullptr;
    QPushButton* openDataButton_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QAction* refreshAction_ = nullptr;
    QTabWidget* tabs_ = nullptr;

    // File/diagnostics menu actions.
    QAction* backupAction_ = nullptr;
    QAction* restoreAction_ = nullptr;
    QAction* verifyHashesAction_ = nullptr;
    QAction* showArgvAction_ = nullptr;
    QAction* testLaunchAction_ = nullptr;
    QAction* editTagsAction_ = nullptr;

    // Library tab.
    QWidget* libraryTab_ = nullptr;
    QTableWidget* libraryTable_ = nullptr;
    QStackedWidget* libraryStack_ = nullptr;
    QWidget* libraryTablePage_ = nullptr;
    QWidget* libraryEmptyPage_ = nullptr;
    QLineEdit* librarySearch_ = nullptr;
    QLabel* libraryCountLabel_ = nullptr;
    QLabel* libraryMessage_ = nullptr;
    QComboBox* groupCombo_ = nullptr;
    QWidget* tagChipsHost_ = nullptr;
    QHBoxLayout* tagChipsLayout_ = nullptr;
    QLabel* tagFilterSummary_ = nullptr;
    QLabel* artworkLabel_ = nullptr;
    QLabel* detailTitle_ = nullptr;
    QLabel* detailState_ = nullptr;
    QLabel* detailPlatform_ = nullptr;
    QLabel* detailRom_ = nullptr;
    QLabel* detailEmulator_ = nullptr;
    QLabel* detailCollection_ = nullptr;
    QLabel* detailTags_ = nullptr;
    QLabel* detailSize_ = nullptr;
    QLabel* detailHash_ = nullptr;
    QLabel* detailHashState_ = nullptr;
    QLabel* detailArguments_ = nullptr;
    QLabel* detailPackage_ = nullptr;
    QPlainTextEdit* detailArgv_ = nullptr;
    QLabel* launchResult_ = nullptr;
    QSpinBox* launchTimeoutSpin_ = nullptr;
    QPushButton* launchButton_ = nullptr;
    QPushButton* argvButton_ = nullptr;
    QPushButton* copyArgvButton_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* editTagsButton_ = nullptr;
    QPushButton* relinkButton_ = nullptr;
    QPushButton* verifyButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* reimportButton_ = nullptr;
    QSplitter* librarySplitter_ = nullptr;

    // Collections panel.
    QListWidget* collectionsList_ = nullptr;
    QLabel* collectionsMessage_ = nullptr;
    QPushButton* collectionAddButton_ = nullptr;
    QPushButton* collectionRenameButton_ = nullptr;
    QPushButton* collectionRemoveButton_ = nullptr;

    // Bulk bar.
    QFrame* bulkBar_ = nullptr;
    QLabel* bulkLabel_ = nullptr;
    QPushButton* bulkCollectionButton_ = nullptr;
    QPushButton* bulkEmulatorButton_ = nullptr;
    QPushButton* bulkAddTagButton_ = nullptr;
    QPushButton* bulkRemoveTagButton_ = nullptr;
    QPushButton* bulkRemoveButton_ = nullptr;
    QPushButton* bulkClearButton_ = nullptr;

    // Import tab.
    QWidget* importTab_ = nullptr;
    QFrame* importDropZone_ = nullptr;
    QTableWidget* importTable_ = nullptr;
    QLineEdit* importCollectionEdit_ = nullptr;
    QLabel* importMessage_ = nullptr;
    QLabel* importCountLabel_ = nullptr;
    QPlainTextEdit* importReport_ = nullptr;
    QProgressBar* importProgress_ = nullptr;
    QPushButton* addFilesButton_ = nullptr;
    QPushButton* addFolderButton_ = nullptr;
    QPushButton* importButton_ = nullptr;
    QPushButton* importPkgButton_ = nullptr;
    QPushButton* clearImportButton_ = nullptr;
    QPushButton* selectAllButton_ = nullptr;
    QPushButton* selectNoneButton_ = nullptr;

    // Emulators tab.
    QWidget* emulatorsTab_ = nullptr;
    QTableWidget* emulatorTable_ = nullptr;
    QLineEdit* emulatorIdEdit_ = nullptr;
    QLineEdit* emulatorNameEdit_ = nullptr;
    QLineEdit* emulatorExecutableEdit_ = nullptr;
    QLineEdit* emulatorWorkingDirectoryEdit_ = nullptr;
    QLineEdit* emulatorPlatformsEdit_ = nullptr;
    QLineEdit* emulatorTemplateEdit_ = nullptr;
    QPlainTextEdit* emulatorEnvEdit_ = nullptr;
    QLineEdit* emulatorSampleRomEdit_ = nullptr;
    QPlainTextEdit* emulatorPreview_ = nullptr;
    QLabel* emulatorPreviewError_ = nullptr;
    QLabel* emulatorStatus_ = nullptr;
    QLabel* emulatorLaunchResult_ = nullptr;
    QPlainTextEdit* emulatorEvidence_ = nullptr;
    QPushButton* emulatorAddButton_ = nullptr;
    QPushButton* emulatorSaveButton_ = nullptr;
    QPushButton* emulatorRemoveButton_ = nullptr;
    QPushButton* emulatorDetectButton_ = nullptr;
    QPushButton* emulatorTestButton_ = nullptr;
    QString editingProfileId_;

    // Platform map tab.
    QWidget* platformMapTab_ = nullptr;
    QTableWidget* mappingTable_ = nullptr;
    QFrame* mappingBanner_ = nullptr;
    QLabel* mappingBannerLabel_ = nullptr;
    QLabel* mappingFileStateLabel_ = nullptr;
    QLabel* mappingStatus_ = nullptr;
    QLabel* mappingEditorTitle_ = nullptr;
    QComboBox* mappingEmulatorCombo_ = nullptr;
    QPlainTextEdit* mappingArgumentsEdit_ = nullptr;
    QLabel* mappingEditorHint_ = nullptr;
    QPushButton* mappingSaveButton_ = nullptr;
    QPushButton* mappingRevertButton_ = nullptr;
    QPushButton* mappingClearOverrideButton_ = nullptr;
    QPushButton* mappingRefreshButton_ = nullptr;

    // Diagnostics tab.
    QWidget* diagnosticsTab_ = nullptr;
    QTreeWidget* layoutTree_ = nullptr;
    QPlainTextEdit* diagnosticsReport_ = nullptr;
    QLabel* diagnosticsMessage_ = nullptr;
    QLineEdit* reportPathEdit_ = nullptr;
    QPushButton* hashVerifyButton_ = nullptr;
    QPushButton* mappingGapButton_ = nullptr;
    QPushButton* exportReportPathButton_ = nullptr;

    // Settings tab.
    QWidget* settingsTab_ = nullptr;
    QLineEdit* settingsDataDirectoryEdit_ = nullptr;
    QLabel* settingsMessage_ = nullptr;
    QWidget* defaultEmulatorHost_ = nullptr;
    QFormLayout* defaultEmulatorForm_ = nullptr;
    QHash<QString, QComboBox*> defaultEmulatorCombos_;
    QComboBox* artworkPolicyCombo_ = nullptr;

    // Status bar and processes.
    QLabel* statusBusyLabel_ = nullptr;
    QVector<QProcess*> launches_;
};

} // namespace xmb::manager
