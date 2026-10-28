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

// ---- The UI-facing switch prompt ------------------------------------------------------------
// One row of the confirmation: the shell renders `body` on a page (or as the page's own text)
// with `confirmLabel` and `cancelLabel` as its two rows. Nothing here performs process work.
struct SteamSwitchStep {
    QString heading;      // the page/dialog heading for this step
    QString body;         // the complete user-facing text, with its own line breaks
    QString confirmLabel; // the row that advances the handoff
    QString cancelLabel;  // the row that leaves it alone
};

// Everything the XMB can honestly show, and ask, for one requested account. The strings are the
// product text, not log lines: they say that Steam must restart, that the XMB does not sign in,
// that no credentials are stored, and who completes the sign-in (the Steam client).
struct SteamSwitchPrompt {
    bool possible = false;     // the handoff can be attempted (steam.exe and the account resolved)
    bool alreadyActive = false; // Steam already names this account as active: nothing to switch
    bool clientSignedIn = false; // Steam's registry reports an active user id
    QString steamExe;           // the resolved client executable
    QString accountLabel;       // persona name, else the login name; empty when none was selected
    QString xmbNote;            // the one-line promise shown in both steps
    QStringList shutdownArguments; // step 1: pass to the running client
    QStringList relaunchArguments; // step 2: pass to the fresh client
    QVector<SteamSwitchStep> steps; // exactly two, in order, when the prompt can proceed
    SteamSwitchStep blocked;    // shown instead of `steps` when it cannot, or nothing is to switch
    QString outcomeText;        // what is true after step 2; never claims a sign-in happened
    QString error;              // machine-facing reason when `possible` is false
};

// Builds the prompt for `account`. Fills `*error` (when given) with the same reason the blocked
// step explains, so a caller can log it without parsing prose. Performs no process work.
SteamSwitchPrompt steamSwitchPlanFor(const SteamAccount& account, const QString& steamRoot = {},
                                     QString* error = nullptr);

// The two-step confirmation model, as a small state machine so the ordering is testable without
// a UI: Blocked (nothing may run) -> ConfirmClose --confirm--> ConfirmRelaunch --confirm--> Ready.
// Only `Ready` means both steps were accepted and the caller may run the two launches. `cancel()`
// from any live stage returns to Blocked/Cancelled and nothing may run.
struct SteamSwitchConfirmation {
    enum class Stage { Blocked, ConfirmClose, ConfirmRelaunch, Ready, Cancelled };
    Stage stage = Stage::Blocked;
    int accepted = 0; // how many of the two steps were confirmed
    bool confirm();   // advances one step; false when the model is not in a confirmable stage
    void cancel();    // leaves the sequence; never returns to a confirmable stage
    bool ready() const { return stage == Stage::Ready; }
};

// A dry run of the same model: walks both steps, records the exact command lines, and performs no
// process work at all (`executed` is always false). Used by tests and by the shell's log line; it
// is also the proof that nothing runs before both confirmations.
struct SteamSwitchDryRun {
    bool possible = false;
    bool executed = false;      // always false: a dry run never starts or stops Steam
    QVector<QString> transcript; // headings, both bodies, the labels pressed, the command lines
    QStringList commands;        // {"<steam.exe> -shutdown", "<steam.exe>"} in handoff order
    SteamSwitchStep blocked;     // set when the prompt cannot proceed
    QString error;
};
SteamSwitchDryRun steamSwitchDryRun(const SteamAccount& account, const QString& steamRoot = {},
                                    QString* error = nullptr);

} // namespace xmb

Q_DECLARE_METATYPE(xmb::SteamAccount)
