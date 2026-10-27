#include "jellyfin_signin.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace xmb {

JellyfinSignIn::JellyfinSignIn(QUrl server, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("jellyfinSignIn"));
    setWindowTitle(QStringLiteral("Sign in to Jellyfin"));
    setModal(true);
    setMinimumWidth(420);

    auto* root = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("Sign in to Jellyfin"), this);
    heading->setObjectName(QStringLiteral("jellyfinSignInHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(14);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    root->addWidget(heading);

    auto* form = new QFormLayout;
    root->addLayout(form);
    serverEdit_ = new QLineEdit(this);
    serverEdit_->setObjectName(QStringLiteral("jellyfinServerUrlEdit"));
    serverEdit_->setAccessibleName(QStringLiteral("Jellyfin server URL"));
    serverEdit_->setText(server.toString(QUrl::FullyEncoded));
    form->addRow(QStringLiteral("Server URL"), serverEdit_);

    usernameEdit_ = new QLineEdit(this);
    usernameEdit_->setObjectName(QStringLiteral("jellyfinUsernameEdit"));
    usernameEdit_->setAccessibleName(QStringLiteral("Jellyfin username"));
    form->addRow(QStringLiteral("Username"), usernameEdit_);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setObjectName(QStringLiteral("jellyfinPasswordEdit"));
    passwordEdit_->setAccessibleName(QStringLiteral("Jellyfin password"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Password"), passwordEdit_);

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("jellyfinSignInError"));
    errorLabel_->setStyleSheet(QStringLiteral("color: #ff8c83; padding: 3px 0;"));
    errorLabel_->setWordWrap(true);
    errorLabel_->setVisible(false);
    root->addWidget(errorLabel_);

    auto* buttons = new QDialogButtonBox(this);
    signInButton_ = buttons->addButton(QStringLiteral("Sign in"), QDialogButtonBox::AcceptRole);
    signInButton_->setObjectName(QStringLiteral("jellyfinSignInButton"));
    signInButton_->setDefault(true);
    cancelButton_ = buttons->addButton(QDialogButtonBox::Cancel);
    cancelButton_->setObjectName(QStringLiteral("jellyfinSignInCancelButton"));
    root->addWidget(buttons);

    connect(signInButton_, &QPushButton::clicked, this, &JellyfinSignIn::submit);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(serverEdit_, &QLineEdit::returnPressed, this, &JellyfinSignIn::submit);
    connect(usernameEdit_, &QLineEdit::returnPressed, this, &JellyfinSignIn::submit);
    connect(passwordEdit_, &QLineEdit::returnPressed, this, &JellyfinSignIn::submit);
}

QUrl JellyfinSignIn::serverUrl() const {
    return QUrl(serverEdit_->text().trimmed(), QUrl::StrictMode);
}

QString JellyfinSignIn::username() const {
    return usernameEdit_->text();
}

QString JellyfinSignIn::password() const {
    return passwordEdit_->text();
}

void JellyfinSignIn::setBusy(bool busy) {
    serverEdit_->setEnabled(!busy);
    usernameEdit_->setEnabled(!busy);
    passwordEdit_->setEnabled(!busy);
    signInButton_->setEnabled(!busy);
    cancelButton_->setEnabled(true);
}

void JellyfinSignIn::showError(const QString& message) {
    errorLabel_->setText(message);
    errorLabel_->setVisible(!message.isEmpty());
}

void JellyfinSignIn::submit() {
    if(!signInButton_->isEnabled())return;
    showError(QString());
    emit signInRequested();
}

} // namespace xmb
