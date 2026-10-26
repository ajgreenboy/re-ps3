#include "app_editor.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>

namespace xmb {
namespace {

QString categoryLabel(const QString& category) {
    if (category == QStringLiteral("game")) return QStringLiteral("Games");
    if (category == QStringLiteral("tv")) return QStringLiteral("TV & video");
    if (category == QStringLiteral("music")) return QStringLiteral("Music");
    if (category == QStringLiteral("video")) return QStringLiteral("Video");
    return QStringLiteral("Network");
}

} // namespace

AppEditor::AppEditor(QString registryPath, QWidget* parent)
    : QDialog(parent), registryPath_(std::move(registryPath)) {
    setObjectName(QStringLiteral("appEditor"));
    setWindowTitle(QStringLiteral("Content registrations"));
    setWindowFlag(Qt::Window, true);
    resize(820, 560);
    setMinimumSize(680, 460);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #171a20; color: #edf0f5; }"
        "QLabel { color: #d9dee8; }"
        "QLineEdit, QComboBox, QTextEdit, QListWidget { background: #20252e; color: #edf0f5;"
        " border: 1px solid #3a424f; border-radius: 4px; padding: 6px; selection-background-color: #356ca8; }"
        "QPushButton { background: #29313d; color: #edf0f5; border: 1px solid #414c5c;"
        " border-radius: 4px; padding: 7px 12px; }"
        "QPushButton:hover { background: #354253; }"
        "QPushButton:default { background: #315f8f; border-color: #4c83bb; }"
        "QPushButton:disabled { color: #77808e; }"));

    auto* root = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("Register games and web apps for the XMB"), this);
    heading->setObjectName(QStringLiteral("appEditorHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(14);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    root->addWidget(heading);

    auto* body = new QHBoxLayout;
    root->addLayout(body, 1);
    auto* left = new QVBoxLayout;
    body->addLayout(left, 1);
    auto* listTitle = new QLabel(QStringLiteral("Registered apps"), this);
    left->addWidget(listTitle);
    appList_ = new QListWidget(this);
    appList_->setObjectName(QStringLiteral("appList"));
    appList_->setAccessibleName(QStringLiteral("Registered apps"));
    left->addWidget(appList_, 1);
    auto* addRow = new QHBoxLayout;
    left->addLayout(addRow);
    auto* addGameButton = new QPushButton(QStringLiteral("Add game…"), this);
    addGameButton->setObjectName(QStringLiteral("addGameButton"));
    auto* addWebButton = new QPushButton(QStringLiteral("Add media app…"), this);
    addWebButton->setObjectName(QStringLiteral("addMediaAppButton"));
    addRow->addWidget(addGameButton);
    addRow->addWidget(addWebButton);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    body->addLayout(form, 2);
    titleEdit_ = new QLineEdit(this);
    titleEdit_->setObjectName(QStringLiteral("appTitleEdit"));
    form->addRow(QStringLiteral("Name"), titleEdit_);
    kindCombo_ = new QComboBox(this);
    kindCombo_->setObjectName(QStringLiteral("appKindCombo"));
    kindCombo_->addItem(QStringLiteral("Game"), QStringLiteral("game"));
    kindCombo_->addItem(QStringLiteral("Web app"), QStringLiteral("web"));
    form->addRow(QStringLiteral("Kind"), kindCombo_);
    categoryCombo_ = new QComboBox(this);
    categoryCombo_->setObjectName(QStringLiteral("appCategoryCombo"));
    categoryCombo_->addItem(QStringLiteral("Games"), QStringLiteral("game"));
    categoryCombo_->addItem(QStringLiteral("TV & video"), QStringLiteral("tv"));
    categoryCombo_->addItem(QStringLiteral("Music"), QStringLiteral("music"));
    categoryCombo_->addItem(QStringLiteral("Video"), QStringLiteral("video"));
    categoryCombo_->addItem(QStringLiteral("Network"), QStringLiteral("network"));
    form->addRow(QStringLiteral("Category"), categoryCombo_);
    targetLabel_ = new QLabel(QStringLiteral("Executable"), this);
    targetEdit_ = new QLineEdit(this);
    targetEdit_->setObjectName(QStringLiteral("appTargetEdit"));
    auto* targetRow = new QWidget(this);
    auto* targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->addWidget(targetEdit_, 1);
    auto* targetBrowse = new QPushButton(QStringLiteral("Browse…"), targetRow);
    targetBrowse->setObjectName(QStringLiteral("targetBrowseButton"));
    targetLayout->addWidget(targetBrowse);
    form->addRow(targetLabel_, targetRow);
    workingDirectoryEdit_ = new QLineEdit(this);
    workingDirectoryEdit_->setObjectName(QStringLiteral("appWorkingDirectoryEdit"));
    auto* workingRow = new QWidget(this);
    auto* workingLayout = new QHBoxLayout(workingRow);
    workingLayout->setContentsMargins(0, 0, 0, 0);
    workingLayout->addWidget(workingDirectoryEdit_, 1);
    auto* workingBrowse = new QPushButton(QStringLiteral("Browse…"), workingRow);
    workingBrowse->setObjectName(QStringLiteral("workingDirectoryBrowseButton"));
    workingLayout->addWidget(workingBrowse);
    form->addRow(QStringLiteral("Working directory"), workingRow);
    artworkEdit_ = new QLineEdit(this);
    artworkEdit_->setObjectName(QStringLiteral("appArtworkEdit"));
    auto* artworkRow = new QWidget(this);
    auto* artworkLayout = new QHBoxLayout(artworkRow);
    artworkLayout->setContentsMargins(0, 0, 0, 0);
    artworkLayout->addWidget(artworkEdit_, 1);
    auto* artworkBrowse = new QPushButton(QStringLiteral("Browse…"), artworkRow);
    artworkBrowse->setObjectName(QStringLiteral("artworkBrowseButton"));
    artworkLayout->addWidget(artworkBrowse);
    form->addRow(QStringLiteral("Artwork"), artworkRow);
    argumentsEdit_ = new QTextEdit(this);
    argumentsEdit_->setObjectName(QStringLiteral("appArgumentsEdit"));
    argumentsEdit_->setAccessibleName(QStringLiteral("Arguments, one argument per line"));
    argumentsEdit_->setPlaceholderText(QStringLiteral("Optional; one argument per line"));
    argumentsEdit_->setMaximumHeight(120);
    form->addRow(QStringLiteral("Arguments"), argumentsEdit_);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("appEditorError"));
    errorLabel_->setStyleSheet(QStringLiteral("color: #ff8c83; padding: 3px 0;"));
    errorLabel_->setWordWrap(true);
    root->addWidget(errorLabel_);

    auto* footer = new QHBoxLayout;
    root->addLayout(footer);
    auto* importButton = new QPushButton(QStringLiteral("Import JSON…"), this);
    importButton->setObjectName(QStringLiteral("importJsonButton"));
    auto* removeButton = new QPushButton(QStringLiteral("Remove registration"), this);
    removeButton->setObjectName(QStringLiteral("removeRegistrationButton"));
    auto* saveButton = new QPushButton(QStringLiteral("Save registration"), this);
    saveButton->setObjectName(QStringLiteral("saveRegistrationButton"));
    saveButton->setDefault(true);
    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);
    closeButton->setObjectName(QStringLiteral("closeAppEditorButton"));
    footer->addWidget(importButton);
    footer->addWidget(removeButton);
    footer->addStretch(1);
    footer->addWidget(saveButton);
    footer->addWidget(closeButton);

    connect(appList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                if (current)
                    populateEditor(current->data(Qt::UserRole).toString());
            });
    connect(addGameButton, &QPushButton::clicked, this, &AppEditor::addGame);
    connect(addWebButton, &QPushButton::clicked, this, &AppEditor::addWebApp);
    connect(importButton, &QPushButton::clicked, this, &AppEditor::importJson);
    connect(saveButton, &QPushButton::clicked, this, &AppEditor::saveSelected);
    connect(removeButton, &QPushButton::clicked, this, &AppEditor::removeSelected);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(kindCombo_, &QComboBox::currentIndexChanged, this, [this](int) { updateFieldState(); });
    connect(targetBrowse, &QPushButton::clicked, this, [this] {
        if (kindCombo_->currentData().toString() == QStringLiteral("game")) {
            const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose game executable"),
                                                               targetEdit_->text(), QStringLiteral("Applications (*.exe);;All files (*)"));
            if (!file.isEmpty()) targetEdit_->setText(file);
        }
    });
    connect(workingBrowse, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose working directory"),
                                                                      workingDirectoryEdit_->text());
        if (!directory.isEmpty()) workingDirectoryEdit_->setText(directory);
    });
    connect(artworkBrowse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose artwork"), artworkEdit_->text(),
                                                           QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All files (*)"));
        if (!file.isEmpty()) artworkEdit_->setText(file);
    });
    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setObjectName(QStringLiteral("saveRegistrationShortcut"));
    connect(saveShortcut, &QShortcut::activated, this, &AppEditor::saveSelected);
    auto* deleteShortcut = new QShortcut(QKeySequence::Delete, appList_);
    deleteShortcut->setObjectName(QStringLiteral("removeRegistrationShortcut"));
    deleteShortcut->setContext(Qt::WidgetShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &AppEditor::removeSelected);

    reloadRegistry();
    updateFieldState();
}

bool AppEditor::reloadRegistry(bool showErrors) {
    QString error;
    QVector<ContentApp> loaded;
    if (!ContentRegistry::load(registryPath_, loaded, error)) {
        if (showErrors) showError(error);
        return false;
    }
    apps_ = std::move(loaded);
    unsavedIds_.clear();
    stagedId_.clear();
    rebuildList();
    errorLabel_->clear();
    return true;
}

void AppEditor::rebuildList(const QString& selectId) {
    appList_->clear();
    int selectedRow = -1;
    for (qsizetype index = 0; index < apps_.size(); ++index) {
        const ContentApp& app = apps_.at(index);
        auto* item = new QListWidgetItem(QStringLiteral("%1  ·  %2")
                                         .arg(app.title, categoryLabel(app.category)), appList_);
        item->setData(Qt::UserRole, app.id);
        item->setToolTip(app.kind == QStringLiteral("game") ? app.executable : app.url);
        if (app.id == selectId) selectedRow = int(index);
    }
    if (!selectId.isEmpty() && selectedRow < 0) {
        const auto staged = std::find_if(apps_.cbegin(), apps_.cend(), [&selectId](const ContentApp& app) {
            return app.id == selectId;
        });
        if (staged != apps_.cend()) selectedRow = int(std::distance(apps_.cbegin(), staged));
    }
    if (appList_->count() > 0) {
        appList_->setCurrentRow(selectedRow >= 0 ? selectedRow : 0);
    } else {
        titleEdit_->clear();
        targetEdit_->clear();
        workingDirectoryEdit_->clear();
        artworkEdit_->clear();
        argumentsEdit_->clear();
        stagedId_.clear();
    }
}

void AppEditor::populateEditor(const QString& id) {
    const auto found = std::find_if(apps_.cbegin(), apps_.cend(), [&id](const ContentApp& app) {
        return app.id == id;
    });
    if (found == apps_.cend()) return;
    titleEdit_->setText(found->title);
    kindCombo_->setCurrentIndex(kindCombo_->findData(found->kind));
    categoryCombo_->setCurrentIndex(categoryCombo_->findData(found->category));
    targetEdit_->setText(found->kind == QStringLiteral("game") ? found->executable : found->url);
    workingDirectoryEdit_->setText(found->workingDirectory);
    artworkEdit_->setText(found->artwork);
    argumentsEdit_->setPlainText(found->arguments.join(QLatin1Char('\n')));
    stagedId_ = id;
    updateFieldState();
}

void AppEditor::updateFieldState() {
    const bool game = kindCombo_->currentData().toString() == QStringLiteral("game");
    targetLabel_->setText(game ? QStringLiteral("Executable") : QStringLiteral("URL"));
    targetEdit_->setPlaceholderText(game ? QStringLiteral("Path to an existing executable")
                                         : QStringLiteral("https://example.com"));
    const auto* targetRow = qobject_cast<QWidget*>(targetEdit_->parent());
    if (targetRow) {
        const auto buttons = targetRow->findChildren<QPushButton*>();
        for (QPushButton* button : buttons) button->setVisible(game);
    }
    workingDirectoryEdit_->setEnabled(game);
    argumentsEdit_->setEnabled(game);
    categoryCombo_->setEnabled(true);
}

ContentApp AppEditor::currentApp(QString& error) const {
    ContentApp app;
    app.id = stagedId_;
    if (app.id.isEmpty())
        app.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    app.title = titleEdit_->text();
    app.kind = kindCombo_->currentData().toString();
    app.category = categoryCombo_->currentData().toString();
    if (app.kind == QStringLiteral("game")) {
        app.executable = targetEdit_->text().trimmed();
        app.workingDirectory = workingDirectoryEdit_->text().trimmed();
        const QStringList lines = argumentsEdit_->toPlainText().split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (!line.isEmpty()) app.arguments.append(line);
        }
    } else {
        app.url = targetEdit_->text().trimmed();
    }
    app.artwork = artworkEdit_->text().trimmed();
    error.clear();
    return app;
}

void AppEditor::showError(const QString& message) {
    errorLabel_->setText(message);
}

void AppEditor::addGame() {
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose game executable"), QString(),
                                                       QStringLiteral("Applications (*.exe);;All files (*)"));
    if (file.isEmpty()) return;
    ContentApp app;
    app.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    app.title = QFileInfo(file).completeBaseName();
    app.kind = QStringLiteral("game");
    app.category = QStringLiteral("game");
    app.executable = QFileInfo(file).absoluteFilePath();
    apps_.append(app);
    unsavedIds_.insert(app.id);
    stagedId_ = app.id;
    rebuildList(app.id);
    errorLabel_->clear();
}

void AppEditor::addWebApp() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Add media app"), QStringLiteral("App name"),
                                                QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted) return;
    if (name.isEmpty()) { showError(QStringLiteral("App name must not be empty.")); return; }
    const QString url = QInputDialog::getText(this, QStringLiteral("Add media app"), QStringLiteral("HTTP(S) URL"),
                                               QLineEdit::Normal, QStringLiteral("https://"), &accepted).trimmed();
    if (!accepted) return;
    const QStringList labels = {QStringLiteral("TV & video"), QStringLiteral("Music"),
                                QStringLiteral("Video"), QStringLiteral("Network")};
    const QStringList values = {QStringLiteral("tv"), QStringLiteral("music"),
                                QStringLiteral("video"), QStringLiteral("network")};
    const QString chosen = QInputDialog::getItem(this, QStringLiteral("Add media app"), QStringLiteral("Category"),
                                                  labels, 0, false, &accepted);
    if (!accepted) return;
    ContentApp app;
    app.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    app.title = name;
    app.kind = QStringLiteral("web");
    app.category = values.value(labels.indexOf(chosen), QStringLiteral("tv"));
    app.url = url;
    apps_.append(app);
    unsavedIds_.insert(app.id);
    stagedId_ = app.id;
    rebuildList(app.id);
    errorLabel_->clear();
}

void AppEditor::importJson() {
    const QString source = QFileDialog::getOpenFileName(this, QStringLiteral("Import content registry"), QString(),
                                                         QStringLiteral("JSON files (*.json);;All files (*)"));
    if (source.isEmpty()) return;
    QString error;
    if (!ContentRegistry::importFile(source, registryPath_, error)) {
        showError(error);
        return;
    }
    if (!reloadRegistry()) return;
    emit registryChanged();
}

void AppEditor::saveSelected() {
    QString error;
    ContentApp app = currentApp(error);
    if (!error.isEmpty()) { showError(error); return; }
    if (!ContentRegistry::upsert(registryPath_, app, error)) {
        showError(error);
        return;
    }
    if (!reloadRegistry()) return;
    rebuildList(app.id);
    emit registryChanged();
}

void AppEditor::removeSelected() {
    const QListWidgetItem* item = appList_->currentItem();
    if (!item) { showError(QStringLiteral("Select an app registration to remove.")); return; }
    const QString id = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, QStringLiteral("Remove registration"),
                              QStringLiteral("Remove the registration for “%1”? The app files will not be changed.")
                                  .arg(item->text().section(QStringLiteral("  ·  "), 0, 0)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    if (unsavedIds_.remove(id)) {
        apps_.erase(std::remove_if(apps_.begin(), apps_.end(), [&id](const ContentApp& app) { return app.id == id; }), apps_.end());
        rebuildList();
        errorLabel_->clear();
        return;
    }
    QString error;
    if (!ContentRegistry::remove(registryPath_, id, error)) {
        showError(error);
        return;
    }
    if (!reloadRegistry()) return;
    emit registryChanged();
}

} // namespace xmb
