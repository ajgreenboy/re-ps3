#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace xmb {

// One Steam account this machine remembers, read from Steam's own local files.
// The source is <steam>/config/loginusers.vdf (login name, persona name, two
// booleans, a timestamp) plus two read-only registry values (the client's active
// user id and its last auto-login name). No password, web token, cookie, SSFN
// file or connect cache is read, stored or logged by this module, and nothing is
// written to the Steam installation.
struct SteamAccount {
    QString steamId64;   // canonical 17-digit individual account id
    QString accountName; // the login name Steam remembers; compared case-insensitively
    QString personaName; // display name, or accountName when the file has none
    bool rememberPassword = false;
    bool autoLogin = false;
    bool active = false;
    bool hasUserdata = false; // userdata/<accountid>/config/localconfig.vdf exists
    qint64 lastLoginUnix = 0; // 0 when the client recorded no usable timestamp
};

// Reads the remembered accounts of the installed Steam client. Root resolution
// order: the explicit argument, then XMB_STEAM_ROOT (also set by --steam-root),
// then HKCU\Software\Valve\Steam\SteamPath, then the default install folders; a
// candidate counts only when its config directory exists. This mirrors how
// scanSteam() (steam_library.h) selects the Steam installation.
//
// Contract:
// - The read is bounded (1 MiB), local-only and read-only.
// - `error` is cleared on entry. Empty account list plus empty error means Steam
//   was found but no account is remembered; a non-empty error means the list
//   could not be read/parsed or Steam was not found. A non-empty error with a
//   non-empty list is only used by the too-many-accounts cap (64).
// - `active` is Steam's own record, not proof of a live signed-in session. It is
//   taken from, in order: the running client's HKCU\...\Steam\ActiveProcess
//   ActiveUser (the key scanSteam() also prefers), then AutoLoginUser, then the
//   file's own AutoLogin marker, then a legacy MostRecent marker or a sole
//   remembered account. A client that reports ActiveUser=0 is treated as signed
//   out and no account is guessed.
// - Accounts are sorted active first, then newest recorded login, then SteamID64.
QVector<SteamAccount> scanSteamAccounts(const QString& steamRoot = {}, QString* error = nullptr);

// The only honest switch handoff. Steam exposes no supported "sign in as this
// account" command; the XMB must hand control back to the client and let the
// client's own sign-in window authenticate. The caller performs two launches:
//   1. steam.exe <steamShutdownArguments()>   (ask a running client to exit)
//   2. steam.exe <steamRelaunchArguments()>   (start it again, no account args)
// The XMB never passes -login, a user name, a password or a token, never claims
// the switch succeeded, and never reports the target account as signed in.
QStringList steamShutdownArguments();
QStringList steamRelaunchArguments();

// Everything the XMB can honestly offer for a requested account.
struct SteamSwitchPlan {
    bool possible = false; // the handoff can be attempted (steam.exe was found)
    bool clientSignedIn = false; // the registry reports an active user id
    bool alreadyActive = false; // Steam already names this account as active
    QString steamExe;           // resolved client executable; empty when missing
    QStringList shutdownArguments; // pass to the running client first
    QStringList relaunchArguments; // pass to the fresh client second
    QString explanation;           // user-facing text; never claims XMB signed in
};

// Builds the handoff for `account`. It does not start or stop any process: the
// caller runs the two launches above, for example after an explicit user
// confirmation. `account` only selects the explanation and the alreadyActive
// check; no credential is derived from it. The process handoff is deliberately
// separate from this module so the XMB keeps one owner for process lifecycle.
SteamSwitchPlan steamAccountSwitchPlan(const SteamAccount& account, const QString& steamRoot = {},
                                       QString* error = nullptr);

} // namespace xmb

Q_DECLARE_METATYPE(xmb::SteamAccount)
