#pragma once
#include <QString>

// ZF3 (audit/AUTOLOGON.md): a read-only report of Windows' automatic sign-in. INFERRED: the firmware has no such row.
// It reads exactly two values under HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon: AutoAdminLogon
// (the string "1" means on) and DefaultUserName. It never reads, writes or tests for DefaultPassword, never opens the
// key for writing and never changes a setting; turning automatic sign-in on is the owner's own step (netplwiz or
// Sysinternals Autologon). The mapping from the values to a status is a pure function so it is tested without a registry.
namespace xmb::autologon {
enum class State {
    Off,               // AutoAdminLogon is absent or is not "1"
    OnThisUser,        // on, and DefaultUserName is the account running the XMB
    OnOtherUser,       // on, and DefaultUserName is a different account
    OnUnverifiedUser,  // on, but the name cannot be compared (empty, or a Microsoft account's e-mail address)
    Unreadable         // the key could not be read at all (not Windows, or access denied)
};
struct Status {
    State state=State::Unreadable;
    QString user;      // DefaultUserName as stored (shown for OnThisUser / OnUnverifiedUser)
    QString value;     // the Settings row's value: "On for <user>", "On for a different user", "Off" or "Unknown"
    QString log;       // the boot log line, e.g. "autologon: on for <user>"; never carries any password information
};
// Pure: the two registry strings (empty when the value is absent), whether the key was readable, and the current user.
Status classify(const QString& autoAdminLogon,const QString& defaultUserName,const QString& currentUser,bool readable=true);
// Reads the two values and the current Windows user, then classify(). Cheap; reads nothing else. XMB_AUTOLOGON_FAKE="<AutoAdminLogon>|<DefaultUserName>|<current user>" (or "unreadable") replaces the reading for scripted checks.
Status read();
// read() cached for a few seconds, for a row that is drawn every frame.
Status current();
// The Settings information page: the status, the manual steps (packaging/README.md wording) and what the XMB never does.
QString pageText(const Status& status);
// The same facts in about nine lines, for the first-run step's message page (the wizard's page does not scroll).
QString shortPageText(const Status& status);
// The first-run step's guidance for this status (short; the full page is in Settings).
QString stepGuidance(const Status& status);
// The one boot log line (qInfo), once per process.
void logOnce();
}
