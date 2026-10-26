#pragma once

#include "content_registry.h"

#include <QDialog>
#include <QSet>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTextEdit;

namespace xmb {

class AppEditor : public QDialog {
    Q_OBJECT

public:
    explicit AppEditor(QString registryPath, QWidget* parent = nullptr);

signals:
    void registryChanged();

private:
    bool reloadRegistry(bool showErrors = true);
    void rebuildList(const QString& selectId = {});
    void populateEditor(const QString& id);
    void updateFieldState();
    ContentApp currentApp(QString& error) const;
    void showError(const QString& message);
    void addGame();
    void addWebApp();
    void importJson();
    void saveSelected();
    void removeSelected();

    QString registryPath_;
    QVector<ContentApp> apps_;
    QString stagedId_;
    QSet<QString> unsavedIds_;

    QListWidget* appList_ = nullptr;
    QLineEdit* titleEdit_ = nullptr;
    QComboBox* kindCombo_ = nullptr;
    QComboBox* categoryCombo_ = nullptr;
    QLabel* targetLabel_ = nullptr;
    QLineEdit* targetEdit_ = nullptr;
    QLineEdit* workingDirectoryEdit_ = nullptr;
    QLineEdit* artworkEdit_ = nullptr;
    QTextEdit* argumentsEdit_ = nullptr;
    QLabel* errorLabel_ = nullptr;
};

} // namespace xmb
