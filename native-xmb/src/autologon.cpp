#include "autologon.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace xmb::autologon {
namespace {
// "DOMAIN\name" -> "name"; the comparison is case-insensitive, as Windows account names are.
QString bareName(QString name) {
    name=name.trimmed();
    const int slash=name.lastIndexOf(QLatin1Char('\\'));
    if(slash>=0)name=name.mid(slash+1);
    return name.trimmed().toCaseFolded();
}
#ifdef Q_OS_WIN
// One REG_SZ under the Winlogon key, opened for reading only. Only ever called with the two names this module reads.
// `ok` goes false when the key itself could not be read; an absent value is not an error.
QString readString(const wchar_t* name,bool* ok) {
    wchar_t buffer[512]{};
    DWORD size=sizeof(buffer)-sizeof(wchar_t);
    const LONG result=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",name,
        RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,buffer,&size);
    if(result==ERROR_SUCCESS)return QString::fromWCharArray(buffer);
    if(result!=ERROR_FILE_NOT_FOUND && result!=ERROR_PATH_NOT_FOUND && result!=ERROR_UNSUPPORTED_TYPE && result!=ERROR_MORE_DATA)*ok=false;
    return {};
}
QString windowsUserName() {
    wchar_t buffer[257]{};
    DWORD size=DWORD(sizeof(buffer)/sizeof(buffer[0]));
    return GetUserNameW(buffer,&size)?QString::fromWCharArray(buffer):QString{};
}
#endif
}

Status classify(const QString& autoAdminLogon,const QString& defaultUserName,const QString& currentUser,bool readable) {
    Status status;
    status.user=defaultUserName.trimmed();
    if(!readable) {
        status.state=State::Unreadable;status.value=QStringLiteral("Unknown");
        status.log=QStringLiteral("autologon: unknown (could not be read)");
        return status;
    }
    if(autoAdminLogon.trimmed()!=QLatin1String("1")) {
        status.state=State::Off;status.value=QStringLiteral("Off");status.log=QStringLiteral("autologon: off");
        return status;
    }
    const QString configured=bareName(status.user),current=bareName(currentUser);
    const bool email=status.user.contains(QLatin1Char('@'));
    if(configured.isEmpty() || current.isEmpty()) {
        status.state=State::OnUnverifiedUser;status.value=QStringLiteral("On");status.log=QStringLiteral("autologon: on (user name not set)");
    } else if(configured==current || (email && configured.section(QLatin1Char('@'),0,0)==current)) {
        status.state=State::OnThisUser;status.value=QStringLiteral("On for ")+status.user;
        status.log=QStringLiteral("autologon: on for ")+status.user;
    } else if(email) {
        // A Microsoft account is stored as its e-mail address while the profile has a shorter name: not comparable, so
        // the row says what is stored and does not claim a mismatch.
        status.state=State::OnUnverifiedUser;status.value=QStringLiteral("On for ")+status.user;
        status.log=QStringLiteral("autologon: on for ")+status.user+QStringLiteral(" (account name not comparable)");
    } else {
        status.state=State::OnOtherUser;status.value=QStringLiteral("On for a different user");
        status.log=QStringLiteral("autologon: on for a different user (")+status.user+QLatin1Char(')');
    }
    return status;
}

Status read() {
    // Test hook for scripted checks of each state without touching the registry (the pattern of XMB_SETUP_FAKE_PAD):
    // XMB_AUTOLOGON_FAKE="<AutoAdminLogon>|<DefaultUserName>|<current user>"; "unreadable" simulates a key that cannot be read.
    if(qEnvironmentVariableIsSet("XMB_AUTOLOGON_FAKE")) {
        const QString fake=qEnvironmentVariable("XMB_AUTOLOGON_FAKE");
        if(fake==QLatin1String("unreadable"))return classify({},{},{},false);
        const QStringList parts=fake.split(QLatin1Char('|'));
        return classify(parts.value(0),parts.value(1),parts.value(2));
    }
#ifdef Q_OS_WIN
    bool ok=true;
    const QString flag=readString(L"AutoAdminLogon",&ok);
    const QString user=readString(L"DefaultUserName",&ok);
    return classify(flag,user,windowsUserName(),ok);
#else
    return classify({},{},{},false);
#endif
}

Status current() {
    static QMutex mutex;
    static QElapsedTimer age;
    static Status cached;
    QMutexLocker lock(&mutex);
    if(!age.isValid() || age.elapsed()>5000){cached=read();age.restart();}
    return cached;
}

QString stepGuidance(const Status& status) {
    switch(status.state) {
    case State::OnThisUser:
        return QStringLiteral("Windows signs this account in by itself at start-up, so the TV goes straight to the XMB. Nothing to do.");
    case State::OnOtherUser:
        return QStringLiteral("Windows signs a different account in at start-up, so this account's XMB sign-in task will not run. Use netplwiz or Sysinternals Autologon yourself to change it.");
    case State::OnUnverifiedUser:
        return QStringLiteral("Windows automatic sign-in is on. The stored account name cannot be compared with this one, so check it in netplwiz if the TV does not reach the XMB.");
    case State::Unreadable:
        return QStringLiteral("The setting could not be read on this PC. Turning it on is your own step, with netplwiz or Sysinternals Autologon.");
    case State::Off:
        break;
    }
    return QStringLiteral("Windows automatic sign-in is off, so after a restart the PC waits at the sign-in screen for a keyboard. To boot straight into the XMB, turn it on yourself with netplwiz or Sysinternals Autologon.");
}

// The information page splits a line at its first colon into a label and a value column and does not wrap, so the body is
// hand-wrapped into short lines and every sentence avoids a colon followed by a space.
QString wrapped(const QString& text,int width) {
    QStringList lines;
    QString line;
    for(const QString& word:text.split(QLatin1Char(' '),Qt::SkipEmptyParts)) {
        if(!line.isEmpty() && line.size()+1+word.size()>width){lines<<line;line.clear();}
        line+=(line.isEmpty()?QString{}:QStringLiteral(" "))+word;
    }
    if(!line.isEmpty())lines<<line;
    return lines.join(QChar::LineFeed);
}

QString pageText(const Status& status) {
    constexpr int kWidth=88;
    // The page does not scroll and holds about sixteen lines, so the steps are the README's wording, shortened.
    const QStringList paragraphs{
        QStringLiteral("Status: ")+status.value,
        stepGuidance(status),
        QStringLiteral("The XMB never asks for, reads or stores the password, and it never changes this setting. It only reads whether automatic sign-in is on and for which account."),
        QStringLiteral("To turn it on yourself, use netplwiz or Sysinternals Autologon. For netplwiz press Win+R, run netplwiz, untick \"Users must enter a user name and password to use this computer\", press Apply and enter the password in Windows' own dialog. Windows 11 may hide the tickbox until Windows Hello only sign-in is switched off in Settings > Accounts > Sign-in options. For Autologon run autologon.exe from Microsoft Sysinternals and enter the user, domain and password once."),
        QStringLiteral("Both store a credential on this PC, so only the owner should do it, in a trusted room.")};
    QStringList blocks{QStringLiteral("Windows automatic sign-in"),QString{}};
    blocks<<paragraphs.first()<<QString{};   // the status keeps its label and value columns
    for(int i=1;i<paragraphs.size();++i){blocks<<wrapped(paragraphs[i],kWidth);blocks<<QString{};}
    blocks.removeLast();
    return blocks.join(QChar::LineFeed);
}

QString shortPageText(const Status& status) {
    const QStringList paragraphs{
        stepGuidance(status),
        QStringLiteral("The XMB never asks for, reads or stores the password, and it never changes this setting."),
        QStringLiteral("To turn it on yourself, use netplwiz (Win+R) or Sysinternals Autologon. Settings > System Settings > Windows automatic sign-in lists the steps.")};
    QStringList blocks{QStringLiteral("Status: ")+status.value,QString{}};
    for(const QString& paragraph:paragraphs){blocks<<wrapped(paragraph,88)<<QString{};}
    blocks.removeLast();
    return blocks.join(QChar::LineFeed);
}

void logOnce() {
    static bool done=false;
    if(done)return;
    done=true;
    qInfo().noquote()<<read().log;
}
}
