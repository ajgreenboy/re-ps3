#pragma once

#include <QDialog>
#include <QString>
#include <QUrl>

class QLabel;
class QLineEdit;
class QPushButton;

namespace xmb {

class JellyfinSignIn : public QDialog {
    Q_OBJECT

public:
    explicit JellyfinSignIn(QUrl server, QWidget* parent = nullptr);

    QUrl serverUrl() const;
    QString username() const;
    QString password() const;
    void setBusy(bool busy);
    void showError(const QString& message);

signals:
    void signInRequested();

private:
    void submit();

    QLineEdit* serverEdit_ = nullptr;
    QLineEdit* usernameEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QPushButton* signInButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
};

} // namespace xmb
