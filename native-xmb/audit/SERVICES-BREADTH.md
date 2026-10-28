# Services breadth: Steam account switch, launch/quit, avatars, trophies and honest rows

Status: Phase 6 of [PHASES-2026-10-27.md](../PHASES-2026-10-27.md), implemented 2026-10-27. This document is
the evidence and the handoff for the parts that need `shell.cpp`/`shell.h` (owned by the Phase 1 work):
every shell edit is given as an exact hunk below and was not applied by this pass.

Scope of the code in this pass: `src/steam_accounts.*`, `src/steam_library.*`, `src/steam_friends.*`.
No other source file, no build file, and no test harness was modified. No real Steam restart, no account
switch, and no game launch or stop were performed while verifying.

What is new, in one table:

| Item | Module | What it now provides | Proven here |
|---|---|---|---|
| 6.1 Account switch UI data layer | `steam_accounts.*` | `steamSwitchPlanFor()` prompt with the exact user-facing strings, a two-step confirmation state machine, and a dry run that never touches Steam | `--plan` dry run, 25 assertions |
| 6.1b Launch/quit lifecycle | `steam_library.*` | Hardened `+app_stop` shape, a bounded `console_log.txt` tail reader, and `steamVerifyStop()` as the client's own acceptance record | `--synthetic` cases, the real 2026-10-26 log line |
| 6.2 Friends avatars | `steam_friends.*` | Bounded avatars by SteamID: profile XML -> allow-listed CDN image -> size/type/dimension checks -> PNG cache -> fallback | real download + cache, offline rejection cases |
| 6.3 Trophy decision | this document | Feasibility verdict and the wording that replaces the placeholder | PS3 trophy model + the machine's data |
| 6.4 Network rows | this document | Exact user-facing reason for manual/browser/search/Remote Play | hunk D |
| 6.5 Disc/media services | this document | Exact user-facing reason for disc titles and the TV/video services row | hunks D and E |

---

## 1. Steam account switch UI (6.1)

### 1.1 What the module already had, and what was missing

`steam_accounts.*` already read Steam's remembered accounts (`loginusers.vdf` plus the client's own
active-user record) and returned a `SteamSwitchPlan`: `steam.exe -shutdown`, then a bare `steam.exe`, with
`alreadyActive` and an explanation. It had no product text, no confirmation model, and no way to show what
would happen. That is what this pass adds.

### 1.2 The prompt: `steamSwitchPlanFor()`

```cpp
struct SteamSwitchStep { QString heading; QString body; QString confirmLabel; QString cancelLabel; };
struct SteamSwitchPrompt {
    bool possible, alreadyActive, clientSignedIn;
    QString steamExe, accountLabel, xmbNote, outcomeText, error;
    QStringList shutdownArguments, relaunchArguments;
    QVector<SteamSwitchStep> steps; // exactly two when the prompt can proceed
    SteamSwitchStep blocked;        // shown instead of steps when it cannot, or nothing is to switch
};
SteamSwitchPrompt steamSwitchPlanFor(const SteamAccount&, const QString& steamRoot = {}, QString* error = nullptr);
```

The strings are the deliverable, so they are quoted whole. The one-line promise, repeated in both steps, is
`SteamSwitchPrompt::xmbNote`:

> Steam must restart; unsaved XMB state is unaffected.

Step 1 (`Switch Steam Account`), confirm `Close Steam`, cancel `Cancel`:

> Steam must restart; unsaved XMB state is unaffected.
>
> Steam will be asked to close. The XMB cannot sign in to Steam and never stores a password, token or
> session file.
>
> Next: start Steam again and sign in as <account> in Steam's own window.

Step 2 (`Start Steam Again`), confirm `Start Steam`, cancel `Not Now`:

> Steam is closing. Start it again to reach Steam's own sign-in window.
>
> Sign in as <account> there. Steam asks for the password and Steam Guard, and the XMB cannot do either
> for you.
>
> Steam picks the account it signs in, so the XMB does not report a switch as done.

After step 2 (`outcomeText`):

> Steam is starting. The account is only known once Steam's own record changes; the XMB re-reads that
> record and never reports a sign-in itself.

`<account>` is the persona name, falling back to the login name. Two blocked cases replace the steps:

- already active: "Steam's own record already names <account> as the active account. Nothing was started.
  To use another account, change it in Steam's own account menu; the XMB cannot sign in."
- no executable / no account selected / Steam not found: the plan's own explanation plus "Nothing was
  started."

Never in these strings: "signed in as", a success claim, a password field, a token, or `-login`. The
assertions in `--plan` check exactly that (`never claims a sign-in`, `no credential flag is passed`,
`relaunch carries no account arguments`).

### 1.3 The two-step model: `SteamSwitchConfirmation`

A four-state machine, so the ordering is testable without a UI:

```
Blocked -> ConfirmClose --confirm--> ConfirmRelaunch --confirm--> Ready
                 |                        |
                 +-------- cancel --------+--> Cancelled (never confirmable again)
```

`confirm()` returns false from `Blocked`, `Ready` and `Cancelled`, so a command can neither run before both
steps nor run twice. `SteamSwitchDryRun steamSwitchDryRun()` walks the same model and records the exact
command lines without performing any process work (`executed` is always false).

### 1.4 Dry-run evidence

Built as a temporary driver (`C:\dev\out\steam6-harness`, source there, not in the repo) against the
repository's modules; the account record used for the strings is synthetic, so no real account name appears:

```
$ steam6-driver.exe --plan
--- prompt for a switchable account
possible=1 alreadyActive=0 clientSignedIn=0 steps=2 exe=C:/program files (x86)/steam/steam.exe
xmbNote: "Steam must restart; unsaved XMB state is unaffected."
shutdownArguments: [-shutdown]
relaunchArguments: []
step1.heading: "Switch Steam Account"
step1.body: Steam must restart; unsaved XMB state is unaffected.
step1.body: Steam will be asked to close. The XMB cannot sign in to Steam and never stores a password, token or session file.
step1.body: Next: start Steam again and sign in as Test Persona in Steam's own window.
step1.confirm: "Close Steam"   step1.cancel: "Cancel"
step2.heading: "Start Steam Again"
step2.confirm: "Start Steam"   step2.cancel: "Not Now"
outcome: Steam is starting. The account is only known once Steam's own record changes; the XMB re-reads that record and never reports a sign-in itself.
dryRun possible=1 executed=0 commands=["C:/program files (x86)/steam/steam.exe" -shutdown | "C:/program files (x86)/steam/steam.exe"]
PASS a switchable account: note is the exact promise
PASS a switchable account: promises the XMB state is unaffected
PASS a switchable account: never claims a sign-in
PASS a switchable account: no credential flag is passed
PASS a switchable account: relaunch carries no account arguments
PASS a switchable account: dry run never executed anything
...
PASS confirmation starts unready / first confirm accepted / still not ready after one step
PASS second confirm accepted / ready only after both steps / third confirm refused
PASS cancelled model can never be ready
failures: 0
```

The already-active and no-selection prompts were run through the same assertions and both pass. No Steam
process was started or stopped by any of this: `executed=0`, and the module contains no process call at all.

### 1.5 Shell hunk A: the page and its confirmation

`shell.h`, next to the other Steam includes:

```cpp
#include "steam_accounts.h"
```

`shell.h`, at the end of `enum class Action` (append; do not renumber existing members):

```cpp
    // The Steam account page (audit/SERVICES-BREADTH.md, 6.1). SteamAccounts opens the remembered list;
    // SteamAccountSwitch opens the two-step confirmation for one row; Confirm/Cancel are the rows the
    // confirmation page itself carries. Appended, so every existing action keeps its value.
    SteamAccounts, SteamAccountSwitch, SteamAccountConfirm, SteamAccountCancel
```

`shell.h`, `Page` (append the two fields; `steamSwitchStep` is -1 on the list page, 0 and 1 on the two
confirmation steps):

```cpp
    struct Page { /* unchanged */ bool signIn=false;
        bool steamAccounts=false; int steamSwitchStep=-1; };
```

`shell.h`, private members and methods:

```cpp
    // Steam account switch: the list Steam itself remembers, the prompt for the chosen row, and the
    // confirmation model that gates the only two process launches this build makes for an account switch.
    QVector<SteamAccount> steamAccounts_;
    SteamSwitchPrompt steamSwitchPrompt_;
    SteamSwitchConfirmation steamSwitchConfirmation_;
    int steamAccountChoice_=0;
    void openSteamAccounts();
    void openSteamAccountSwitch(int accountIndex);
    void confirmSteamAccountSwitch();
    void cancelSteamAccountSwitch();
    QVector<Entry> accountSwitchStepRows(int step) const;
```

`shell.cpp`, in the Game column's provider block in `refreshContent()` (directly after the Steam Store
provider is pushed, before the All Games folder):

```cpp
                // The account this column's library belongs to. The console's Users column owns local
                // users; Steam owns its own sign-in, so this row is the honest handoff, not a login form.
                {
                    auto e=item("steam-account",QObject::tr("Steam Account"),"item_tex_Profile_Onlines",
                        "explore_plugin_full",Action::SteamAccounts,QString());
                    e.detail=QObject::tr("Steam's own account list");
                    providers.push_back(std::move(e));
                }
```

`shell.cpp`, `activate()` (one case per new action, next to `Action::SteamDownloads`):

```cpp
    case Action::SteamAccounts:openSteamAccounts();break;
    case Action::SteamAccountSwitch:openSteamAccountSwitch(entry.option.toInt());break;
    case Action::SteamAccountConfirm:confirmSteamAccountSwitch();break;
    case Action::SteamAccountCancel:cancelSteamAccountSwitch();break;
```

`shell.cpp`, the page builders (place them next to `openSteamDownloads()`):

```cpp
// --- Steam account switch (audit/SERVICES-BREADTH.md, 6.1) --------------------------------------
// The page lists only what Steam itself remembers: scanSteamAccounts() reads loginusers.vdf plus the
// client's own active-user record. Cross on a row opens the two-step confirmation that
// steamSwitchPlanFor() builds; only after both steps does this shell hand two launches to Steam. The
// XMB never signs in, never stores a credential and never reports a switch as done: after the handoff
// the client owns the sign-in window, and F5 re-reads Steam's own record.
QVector<Entry> Shell::accountSwitchStepRows(int step) const {
    QVector<Entry> rows;
    if(step<0 || step>=steamSwitchPrompt_.steps.size())return rows;
    const auto& current=steamSwitchPrompt_.steps.at(step);
    // The body is a row of its own because this build has no multi-line dialog: Cross on it shows the
    // whole step, and its detail line carries the promise while the row is focused.
    auto body=item("steam-account:step",current.heading,"item_tex_Profile_Onlines","explore_category_friend",
        Action::Information,current.body);
    body.detail=steamSwitchPrompt_.xmbNote;
    rows.push_back(std::move(body));
    rows.push_back(item("steam-account:confirm",current.confirmLabel,"item_tex_Plain",
        "explore_category_friend",Action::SteamAccountConfirm));
    rows.push_back(item("steam-account:cancel",current.cancelLabel,"item_tex_Plain",
        "explore_category_friend",Action::SteamAccountCancel));
    return rows;
}
void Shell::openSteamAccounts() {
    QString error;
    steamAccounts_=scanSteamAccounts(steamRoot_,&error);
    QVector<Entry> rows;
    for(int i=0;i<steamAccounts_.size();++i) {
        const auto& account=steamAccounts_.at(i);
        const QString label=account.personaName.isEmpty()?account.accountName:account.personaName;
        auto e=item("steam-account:"+account.steamId64,label,"tex_Avatar_Default","explore_category_friend",
            Action::SteamAccountSwitch,QString::number(i));
        QStringList facts;
        if(account.active)facts<<QObject::tr("Active");
        if(!account.hasUserdata)facts<<QObject::tr("No local userdata yet");
        e.detail=facts.join(QStringLiteral(" - "));
        rows.push_back(std::move(e));
    }
    auto about=item("steam-account:about",QObject::tr("About Steam Accounts"),"item_tex_Profile_Onlines",
        "explore_category_friend",Action::Information,
        QObject::tr("Steam accounts are managed by Steam.\nThe XMB can list the accounts Steam remembers on this "
            "PC and ask Steam to restart, but it cannot sign in, sign out or switch the account.\n"
            "Steam must restart; unsaved XMB state is unaffected."));
    rows.push_back(std::move(about));
    if(!error.isEmpty())rows.push_back(item("steam-account:error",QObject::tr("Steam Accounts Unavailable"),
        "item_tex_Profile_Onlines","explore_category_friend",Action::Information,error));
    const QString title=QObject::tr("Steam Account");
    if(steamAccounts_.isEmpty() && error.isEmpty()) {
        message_=title+"\n\n"+QObject::tr("Steam remembers no accounts on this PC.");
        emit changed();
        return;
    }
    Page page;page.title=title;page.entries=std::move(rows);page.steamAccounts=true;
    pages_.push_back(std::move(page));
    qInfo().noquote()<<QStringLiteral("steam accounts: page opened with %1 account(s)%2")
        .arg(steamAccounts_.size()).arg(error.isEmpty()?QString():QStringLiteral(", error: ")+error);
    emit changed();
}
void Shell::openSteamAccountSwitch(int accountIndex) {
    if(accountIndex<0 || accountIndex>=steamAccounts_.size())return;
    steamAccountChoice_=accountIndex;
    QString error;
    steamSwitchPrompt_=steamSwitchPlanFor(steamAccounts_.at(accountIndex),steamRoot_,&error);
    Page page;page.title=QObject::tr("Steam Account");
    if(steamSwitchPrompt_.steps.size()!=2) {
        // Nothing to confirm: the prompt's blocked step says why (already active, no executable, no
        // account) and the page carries that text instead of inventing a choice.
        page.entries.push_back(item("steam-account:blocked",steamSwitchPrompt_.blocked.heading,
            "item_tex_Profile_Onlines","explore_category_friend",Action::Information,
            steamSwitchPrompt_.blocked.body));
        page.steamAccounts=true;
        pages_.push_back(std::move(page));
        qInfo().noquote()<<"steam accounts: switch blocked:"<<steamSwitchPrompt_.blocked.body;
        emit changed();
        return;
    }
    steamSwitchConfirmation_=SteamSwitchConfirmation{};
    steamSwitchConfirmation_.stage=SteamSwitchConfirmation::Stage::ConfirmClose;
    page.steamAccounts=true;page.steamSwitchStep=0;page.entries=accountSwitchStepRows(0);page.selection=0;
    pages_.push_back(std::move(page));
    qInfo().noquote()<<"steam accounts: confirmation opened for one remembered account";
    emit changed();
}
void Shell::confirmSteamAccountSwitch() {
    if(pages_.isEmpty() || !pages_.last().steamAccounts || pages_.last().steamSwitchStep<0)return;
    if(steamSwitchPrompt_.steps.size()!=2 || steamSwitchPrompt_.steamExe.isEmpty())return;
    auto& page=pages_.last();
    // The same model the dry run walks: nothing runs before both steps are accepted.
    if(!steamSwitchConfirmation_.confirm())return;
    if(page.steamSwitchStep==0) {
        // Step 1 accepted. Steam is still running: nothing has been started or stopped yet.
        page.steamSwitchStep=1;
        page.entries=accountSwitchStepRows(1);
        page.selection=0;
        qInfo().noquote()<<"steam accounts: step 1 confirmed; Steam has not been touched yet";
        emit changed();
        return;
    }
    const auto prompt=steamSwitchPrompt_;
    // Step 2 accepted: the only place this build starts or stops Steam for an account switch.
    if(!QProcess::startDetached(prompt.steamExe,prompt.shutdownArguments)) {
        pages_.removeLast();
        message_=QObject::tr("Switch Steam Account")+"\n\n"+QObject::tr("Steam could not be asked to close.");
        emit changed();
        return;
    }
    qInfo().noquote()<<"steam accounts: asked Steam to close; the relaunch follows in 5 s";
    // The client exits on its own schedule and this shell holds no handle to it, so the relaunch waits a
    // fixed time. The relaunch passes no account argument: Steam decides whether it auto-signs-in or
    // shows its sign-in window.
    const QString executable=prompt.steamExe;
    const QStringList relaunch=prompt.relaunchArguments;
    QTimer::singleShot(5000,this,[executable,relaunch] {
        if(!QProcess::startDetached(executable,relaunch))
            qWarning().noquote()<<"steam accounts: Steam could not be started again";
        else qInfo().noquote()<<"steam accounts: Steam started again without an account argument";
    });
    if(!pages_.isEmpty())pages_.removeLast(); // back to the account list; the outcome is the dialog
    message_=QObject::tr("Switch Steam Account")+"\n\n"+prompt.outcomeText;
    emit changed();
}
void Shell::cancelSteamAccountSwitch() {
    if(pages_.isEmpty() || !pages_.last().steamAccounts || pages_.last().steamSwitchStep<0)return;
    steamSwitchConfirmation_.cancel();
    pages_.removeLast();
    qInfo().noquote()<<"steam accounts: switch cancelled; nothing was started";
    emit changed();
}
```

`shell.cpp`, `pageKind()` (after the download branch):

```cpp
    if(page.steamAccounts)return page.steamSwitchStep>=0?QStringLiteral("steamAccountSwitch")
                                                        :QStringLiteral("steamAccounts");
```

`shell.cpp`, `refreshDirectory()` (next to the download and Continue Watching branches) so F5 re-reads
Steam's own record instead of leaving stale active markers:

```cpp
    if(!mediaOpen_ && !pages_.isEmpty() && pages_.last().steamAccounts && pages_.last().steamSwitchStep<0) {
        const QString focused=focusedRowId();
        openSteamAccounts();
        if(!pages_.isEmpty()) {
            for(int i=0;i<pages_.last().entries.size();++i)
                if(pages_.last().entries[i].id==focused){pages_.last().selection=i;break;}
        }
        return;
    }
```

Known limits of hunk A, stated rather than hidden: the relaunch delay is a fixed five seconds because the
XMB has no handle to the client; the page is English only, like every other page; and the account list is
not persisted (it is re-read from Steam's files on every open).

---

## 2. Launch and quit lifecycle (6.1b)

### 2.1 The command shape, verified against the real client log

The shape is `steam.exe +app_stop <numeric id>`, with the id as its own argument and nothing else. On this
machine's real `Steam\logs\console_log.txt` (508,319 bytes, read-only, shared read) the 2026-10-26 probe is
still recorded at byte 499,462:

```
[2026-10-26 20:40:48] ExecCommandLine: ""C:\Program Files (x86)\Steam\steam.exe" +app_stop 999999999"
```

and there is no `>>> command not found: app_stop` anywhere in the file, which is the second half of the
acceptance signature: the dispatcher logs that line for a command it does not know.

`steamStopArguments()` now owns the shape and validates the id (decimal, non-zero, at most ten digits,
at most `0xffffffff`, nothing else), and `steamStopApp()` uses it. The old version only checked that the
string parsed as a number, so a ten-digit-plus or otherwise non-id argument could be forwarded verbatim.

### 2.2 The bounded reader

`steam_library.h`:

```cpp
struct SteamConsoleLogSlice {
    bool available, truncated;
    qint64 offset, size;
    QByteArray bytes;
    QString path, error;
};
SteamConsoleLogSlice steamConsoleLogTail(const QString& steamRoot, qint64 offset,
                                        qsizetype maximumBytes = 64 * 1024);
qint64 steamConsoleLogSize(const QString& steamRoot);
struct SteamStopEvidence {
    bool logAvailable, delivered, rejected;
    QString commandLine, context, error;
};
SteamStopEvidence steamVerifyStop(const QString& appId, qint64 offset, const QString& steamRoot = {});
```

Properties, all exercised:

- only `<root>/logs/console_log.txt` is opened, read-only, and the root resolution is the same
  `XMB_STEAM_ROOT` -> registry -> default-folder order the rest of the library scanner uses;
- at most 64 KiB by default, hard-capped at 1 MiB; `offset = -1` reads the tail;
- an offset past the end is treated as rotation/truncation, reads the tail and says so, so a rotated log
  can never be read as "the command was not delivered";
- `steamVerifyStop()` matches the exact command line (timestamp prefix stripped, quotes removed, the token
  after `+app_stop` compared to the id), then treats a following `command not found` line as a rejection
  and stops the collected context at the next `ExecCommandLine`.

### 2.3 Evidence

Synthetic log (200 unrelated lines, a different app id first, then the probed id, then a rejection case):

```
PASS synthetic: log size readable :: size=10490
PASS synthetic: delivered detected :: ExecCommandLine: ""C:/Program Files (x86)/Steam/steam.exe" +app_stop 424242"
PASS synthetic: context stops at the next command line :: AppID 424242 (Test):
PASS synthetic: a different id does not match
PASS synthetic: snapshot excludes earlier commands
PASS synthetic: rejection detected :: >>> command not found: app_stop
PASS synthetic: rotated log marked truncated
PASS synthetic: missing log reported
PASS synthetic: explicit cap honoured :: bytes=4096
PASS synthetic: hard cap is 1 MiB :: bytes=1048576
PASS synthetic: offset -1 reads the tail :: offset=3144707
PASS stop arguments: numeric id / non-numeric refused / zero refused / 32-bit overflow refused / injection refused
failures: 0
```

Real log, through the production reader:

```
$ steam6-driver.exe --verify 999999999 499462
log available: 1
delivered: 1
rejected: 0
command line: ExecCommandLine: ""C:\Program Files (x86)\Steam\steam.exe" +app_stop 999999999"
PASS verify: the real log carries the stop command line
PASS verify: the dispatcher did not reject app_stop
```

### 2.4 New finding: the console channel needs a signed-in client

On 2026-10-27 the client on this machine was running but parked at its sign-in screen:
`HKCU\Software\Valve\Steam\ActiveProcess\pid = 20880`, `ActiveUser = 0`, and `console_log.txt` had not been
written since 17:09:11. In that state:

- `steam.exe +apps_running` exited 0 and wrote nothing to `console_log.txt`;
- `steam.exe +app_status 202970` from inside the Steam directory returned immediately and wrote nothing;
- `steam.exe +app_stop 999999999` (the production handoff, which reports only that the forwarding process
  started) also wrote nothing, so `steamVerifyStop()` correctly answered `delivered=false`.

So the `+command` channel documented in [STEAM-CLI.md](STEAM-CLI.md) is available on a running, signed-in
client (as it was on 2026-10-26) and was not available while the client sat at its sign-in screen. Two
consequences, both encoded in the code:

1. A missing log line is **not** proof that a command was malformed or rejected; it is also what a
   signed-out client produces. `steamVerifyStop()` therefore reports `delivered=false` with a reason, and
   never turns that into "the stop failed".
2. The registry remains the primary stop confirmation. The log is only the secondary, client-authored
   record that the command reached the dispatcher.

### 2.5 Shell hunk B: registry first, log second, and no premature "closed"

`shell.h`, next to `steamGameId_`:

```cpp
    QString steamStopPending_;     // the app id a stop handoff was sent for, while its return is pending
    qint64 steamStopLogOffset_=-1; // console_log.txt size taken before that handoff, for steamVerifyStop()
```

`shell.cpp`, the PS menu's Quit Game branch (replace the existing Steam block and the message below it):

```cpp
        if(!steamGameId_.isEmpty()) {
            // "+app_stop" has no return channel: the forwarding process exits immediately. Snapshot the
            // client's log first, then read the appended region once the registry reports the return
            // (steamVerifyStop is the secondary confirmation, never the primary one).
            QString stopError;
            steamStopLogOffset_=steamConsoleLogSize(steamRoot_);
            if(steamStopApp(steamGameId_,steamRoot_,&stopError)) {
                steamStopPending_=steamGameId_;
                qInfo().noquote()<<"shell: asked Steam to stop"<<steamGameId_
                    <<"(console log offset"<<steamStopLogOffset_<<")";
            } else {
                qWarning().noquote()<<"shell: Steam stop failed:"<<stopError;
            }
        }
```

and immediately after it, replace `message_=gameTitle_+"\n\n"+QObject::tr("The game was closed.");` with:

```cpp
        // Steam owns the process: this build only asked. Claiming it closed here would be false until the
        // registry clears, which is what pollSteamGame() reports.
        message_=gameTitle_+"\n\n"+QObject::tr("Steam was asked to close this title. The XMB reports the "
            "return when Steam says the title is no longer running.");
```

`shell.cpp`, `pollSteamGame()`, in the branch that reports the return (after
`qInfo()<<"returned from"<<title<<"(Steam reported it stopped)";`):

```cpp
    if(!steamStopPending_.isEmpty()) {
        // Secondary evidence only. A signed-out client, a rotated log or a title closed from Steam itself
        // legitimately has no line here, so this can change the trace but never the outcome.
        const auto evidence=steamVerifyStop(steamStopPending_,steamStopLogOffset_,steamRoot_);
        qInfo().noquote()<<"steam stop evidence:"
            <<(evidence.delivered?QStringLiteral("command logged by the client")
                                 :QStringLiteral("no command line in the console log"))
            <<(evidence.rejected?QStringLiteral("(the dispatcher rejected app_stop)")
                                :QString())
            <<(evidence.error.isEmpty()?QString():QStringLiteral("(")+evidence.error+QStringLiteral(")"));
        steamStopPending_.clear();
        steamStopLogOffset_=-1;
    }
```

`shell.cpp`, the start-timeout branch of the same function, so a title that never runs does not leave a
pending id behind:

```cpp
        steamWatch_.stop();const QString title=gameTitle_;steamGameId_.clear();gameTitle_.clear();
        steamStopPending_.clear();steamStopLogOffset_=-1;
```

### 2.6 What can only be proven with a real title

The reader and the shape are proven; these are not, and this document does not claim them:

- that an accepted `+app_stop` actually stops a running title, and how long it takes;
- that `Apps\<id>\Running` and `RunningAppID` both clear (the two-poll return this shell already uses) and
  that a launcher-to-game handover does not look like a return;
- whether the client writes the `ExecCommandLine` line for `app_stop` at all while a game is running, and
  whether it writes anything else for the command (the "secondary confirmation" is exactly this);
- unsaved-progress behaviour, the return notification's timing, and `force:1` (never used by this build).

Proving them needs one real launch and one scripted stop, with `STEAM-DOWNLOADS.md`-style captures and the
`console_log.txt` region as the record. That is a user-agreed run, not a test.

---

## 3. Friends avatars (6.2)

### 3.1 Contract

`SteamFriendAvatars` in `steam_friends.*`:

- input is the SteamID the friends list already returns (client helper or Web API fallback);
- `GET https://steamcommunity.com/profiles/<steamid>?xml=1` - https, that exact host, no user-info, no
  cookies, at most 256 KiB, 8 s deadline, at most two redirects and only to the same host;
- the avatar URL is read from `<avatarMedium>` (preferred, it matches the 64 px target) or
  `<avatarFull>`, and is accepted only if it is https on `steamstatic.com`/`*.steamstatic.com` or the older
  `steamcdn-a.akamaihd.net` / `steamcommunity-a.akamaihd.net` hosts. `avatarUrlForProfileXml()` is the pure
  parser, exposed so the allow-list is testable offline;
- the image request must answer `Content-Type: image/...`, at most 1 MiB, and decode with format detection
  limited to JPEG/PNG/WebP and at most 1024 px a side;
- the decoded image is scaled to 64x64 and written atomically (`QSaveFile`, PNG) to
  `<data>/cache/steam/avatars/<steamid>.image`;
- a cached file younger than 30 days is reused without a request; at most 16 avatars are fetched per
  refresh, two at a time, and a superseded refresh cancels its replies;
- `cachedAvatars()` decodes at most 64 cached files for the rows about to be drawn, and omits anything
  missing or unreadable. An absent id is the fallback signal: the shell keeps the firmware's
  `tex_Avatar_Default` icon. No placeholder is generated and no avatar is invented.

### 3.2 Evidence

```
$ steam6-driver.exe --avatar-fetch <data>        # the selected account's own public avatar, id never printed
fetched image: 64x64
cache file: 8194 bytes, decoded=1
PASS avatar fetch: a real avatar was downloaded and cached
PASS avatar fetch: a fresh cache file suppresses the fetch

$ steam6-driver.exe --avatars <data>
PASS avatar: cache path is the id :: .../76561198000000011.image
PASS avatar: invalid id has no path
PASS avatar: cached avatar decoded :: size=64x64
PASS avatar: oversized avatar rejected            # a 2048x2048 cache file is never decoded
PASS avatar: corrupt avatar rejected              # 23 bytes of text
PASS avatar: missing avatar is absent (fallback)
PASS avatar: no fabricated image
PASS avatar: decode cap honoured
PASS avatar: avatarMedium preferred :: https://avatars.steamstatic.com/..._medium.jpg
PASS avatar: avatarFull fallback on a steamstatic host
PASS avatar: http rejected
PASS avatar: foreign host rejected                # https://evil.example/...
PASS avatar: private profile falls back           # an XML page with no avatar tags
failures: 0
```

### 3.3 Shell hunk C: show them, no view change

`shell.h`:

```cpp
    std::unique_ptr<SteamFriendAvatars> steamAvatars_; // friends' own pictures, cached under the data dir
    QHash<QString,QImage> friendAvatars_;              // decoded once per rebuild, keyed by SteamID
```

`shell.cpp`, the constructor's initializer list (after `steamArtwork_`):

```cpp
    steamAvatars_(std::make_unique<SteamFriendAvatars>(QDir(dataDirectory).filePath("cache/steam/avatars"))),
```

`shell.cpp`, in the constructor next to the `steamFriends_` connections:

```cpp
    // A friend's own avatar arrives after the row exists, so only the matching rows are updated. A friend
    // with no picture keeps tex_Avatar_Default: the same fallback the row uses today.
    connect(steamAvatars_.get(),&SteamFriendAvatars::loaded,this,[this](const QString& steamId,const QImage& image){
        if(image.isNull())return;
        friendAvatars_.insert(steamId,image);
        for(int c=0;c<categories_.size();++c)if(categories_[c].id=="friends")
            for(auto& e:categories_[c].entries)if(e.id=="steam-friend:"+steamId)e.artwork=image;
        emit changed();
    });
```

`shell.cpp`, `rebuildFriends()` (after `entries.clear();`):

```cpp
        // Decoded once per rebuild, bounded to 64 files: a refresh cannot turn into 46 reads per frame.
        const QString avatarCache=QDir(dataDirectory_).filePath("cache/steam/avatars");
        if(friendAvatars_.isEmpty()) {
            QStringList ids;
            for(const auto& person:steamFriendList_)ids.push_back(person.steamId);
            friendAvatars_=SteamFriendAvatars::cachedAvatars(avatarCache,ids);
        }
```

inside the friend row loop, right after the row is created:

```cpp
            e.artwork=friendAvatars_.value(person.steamId); // empty when nothing is cached: the fallback icon
```

and at the end of the same branch (after the rows are built):

```cpp
        // Fetch what is missing, unless the user asked for a fully offline build.
        if(qEnvironmentVariable("XMB_STEAM_AVATARS")!="0") {
            QStringList missing;
            for(const auto& person:steamFriendList_)
                if(!friendAvatars_.contains(person.steamId))missing.push_back(person.steamId);
            if(!missing.isEmpty())steamAvatars_->refresh(missing);
        }
```

`shell.cpp`, in `refreshContent()` where the account change is handled, so a stale picture cannot survive an
account switch:

```cpp
        if(accountChanged){steamFriendList_.clear();friendAvatars_.clear();friendsAge_.invalidate();rebuildFriends();}
```

`shell.cpp`, the destructor (next to the `steamArtwork_` disconnect):

```cpp
    disconnect(steamAvatars_.get(),nullptr,this,nullptr);
```

**No view hunk is needed.** `view.cpp` already draws `Entry::artwork` for every non-gameTile row at the icon
quad and keeps the presence dot on top of it:

```cpp
else if(!entry.artwork.isNull()) { ... draws entry.artwork at iconWidth x iconHeight ... }
...
if(entry.friendRow) { ... presence dot ... }
```

A 64x64 avatar is drawn at 46x46 with keep-aspect, exactly where `tex_Avatar_Default` was, and the presence
dot is unchanged. A round crop is a deliberate non-goal: it would be a renderer change with its own layer
baseline, and the firmware's own friend rows use the picture as-is.

### 3.4 Limits

- The profile must be public. A private profile has no avatar fields, so the row keeps the default icon;
  there is no fallback to a Web API request (that would need a key, and the key is not a requirement for
  this feature).
- 30 days of staleness is deliberate: avatars are not worth one request per contact per refresh.
- 16 fetches per refresh and two in flight; a very large friends list fills in over several refreshes.
- Profile pages are read without cookies or a session, so a profile that redirects to a login wall simply
  falls back.

---

## 4. Trophy decision (6.3)

### 4.1 What the catalogue asks for, and what exists today

[FEATURE-CATALOG.md](FEATURE-CATALOG.md) carries two trophy rows: **XMB-284 Trophy Collection**
("Not implemented as a complete original workflow", PSN group, required backend "Authenticated
account/session, network service adapters, trophies, entitlements and commerce") and **XMB-060 System
Settings > Trophy Notifications**. In the build, the PSN column's `Trophy Collection` row is
`Action::Unavailable`, so Cross shows the generic "This feature is not available in the native recreation
yet." The `snd_trophy` cue exists in `interaction_audio` but has no producer.

### 4.2 The PS3 trophy model, and where its data lives

From the PS3 developer wiki's trophy file model (archived
[Trophy files](https://web.archive.org/web/20250130085801/https://www.psdevwiki.com/ps3/Trophy_files), read
2026-10-27): trophies are installed per user under
`dev_hdd0/home/<userid>/trophy/<NPCOMMID>/`:

| File | Content | Readable without console keys |
|---|---|---|
| `TROPCONF.SFM` | trophy set definitions: `<npcommid>`, `<title-name>`, `<title-detail>`, then one `<trophy id hidden ttype pid gid>` with `<name>`/`<detail>`; `ttype` is P/G/S/B (Platinum/Gold/Silver/Bronze). A 160-byte signature header precedes the plain XML body. | yes (plain XML after the header) |
| `ICON0.PNG`, `TROPxxx.PNG` | set icon and one icon per trophy | yes (plain PNG) |
| `TROPUSR.DAT` | per-user state: which trophies are unlocked and their timestamps; encrypted and signed | no (needs the console's trophy key) |
| `TROPTRNS.DAT` | the PSN-sync copy of the above; encrypted and RSA/HMAC signed | no |
| `_TROPSYS_/TROPSYS.DAT` | the user's index of installed trophy sets | not needed for a local list |

Disc and PSN titles ship the installer as `PS3_GAME/TROPDIR/<NPCOMMID>/TROPHY.TRP`; trophies only exist
for games built with SDK 2.40 or later.

### 4.3 What this machine can supply

- The application starts titles through RPCS3 and reads that emulator's `dev_hdd0`. On 2026-10-27,
  `C:\dev\rpcs3\bin\dev_hdd0\home\00000001` contains **no `trophy` directory** (a recursive search found
  none), so even the readable half of the model has zero rows to show here.
- Steam titles never write PS3 trophy data at all. Steam achievements are a different system (Valve's
  `ISteamUserStats`, or the Web API's `GetPlayerAchievements` with a key and an app id).

### 4.4 Options with the data source each one needs

| Option | Data source | Verdict |
|---|---|---|
| Local PS3 trophy list for RPCS3 titles | `dev_hdd0/home/<userid>/trophy/<NPCOMMID>/TROPCONF.SFM` (definitions) + `TROPxxx.PNG` | **Possible for definitions only.** Names, descriptions, grades and icons are readable; the earned/unearned state and timestamps are in `TROPUSR.DAT`, which is encrypted with the console's trophy key. So the list would show every trophy as "not earned" - a wrong claim, not a view. |
| Local list including earned state | `TROPUSR.DAT`, decrypted with the console trophy key | **Out of scope by policy.** It needs console key material, the same class of secret this project deliberately does not touch (compare the account module's refusal to read `ssfn*`), and it is the PS3 platform's DRM. |
| PSN trophy sync | Sony's PSN authentication + trophy service | **No backend.** There is no PSN account, no Sony client API, and no legal or technical path this project has. |
| Steam achievements | Local client helper (`ISteamUserStats`) or Web API `GetPlayerAchievements` | A different feature, with its own data source; not "Trophy Collection". |

### 4.5 Verdict

**No trophy backend exists in this build.** A local trophy *view* cannot honestly be built here today: the
only readable data is the static definition table, and there is no trophy installation on this machine's
`dev_hdd0`. The earned state - the part a trophy list is for - requires decrypting `TROPUSR.DAT` with
console keys, which this project does not do. The row therefore stays unavailable, and its wording says
exactly why (hunk D, below). If RPCS3 later writes trophy data and a definitions-only list is wanted, the
data source is the `TROPCONF.SFM`/`TROPxxx.PNG` pair above; the earned state would still need the key, so the
row's text must then say "earned state unavailable" rather than showing zeros.

---

## 5. Network column wording (6.4)

### 5.1 The gap

`resolveNetworkColumn()` already decides `Action::Unavailable` for the manual, browser, search and Remote
Play rows and logs a reason (`reason=` in the `XMB_XMBML_LOG` line), but the reason is **only logged**:
Cross on the row falls through to the generic sentence "This feature is not available in the native
recreation yet." The user cannot see why. These rows are the honest-unavailable requirement of 6.4, so the
reason has to reach the screen and the catalogue dump.

### 5.2 Exact user-facing wording

| XMBML key | Row | Text shown when the row is opened |
|---|---|---|
| `seg_manual` | Online Instruction Manuals | The console opened each game's manual in the PS3 Internet Browser, from Sony's servers. This build has no browser and no manual service, so there is no page to open. |
| `seg_browser` | Internet Browser | The PS3 Internet Browser is not part of this build, and a Network row carries no URL to hand to the PC browser. |
| `seg_kensaku` | Internet Search | Internet Search was the PS3 browser's search page. This build has no browser; search on the PC instead. |
| `seg_premo` | Remote Play | Remote Play needs a PS3 and a registered PSP or PS Vita. A PC has no equivalent, and the usual PC alternative would take over this machine, so this build does not offer it. |
| `seg_dlctrl` | Download Management | works: opens Steam's download queue (unchanged). |

No row asks for a PSN login or offers a purchase. The PSN column's three rows carry their own text (same
hunk):

| Row | Text |
|---|---|
| Sign In | There is no PlayStation Network sign-in in this build. PSN sign-in needs Sony's authentication service; the XMB does not ask for a password it cannot verify. |
| Trophy Collection | Trophy Collection (PSN) / No trophy backend exists here. PlayStation Network trophies need PSN sign-in and Sony's trophy service. This build also has no local trophy data: RPCS3's dev_hdd0 has no trophy folder, and a PS3 title's earned trophies live in TROPUSR.DAT, which is encrypted with console keys the XMB does not use. Steam's own achievements are a different system and are not shown either. |
| PlayStation Store | This build cannot buy anything. The console store needed PSN sign-in and Sony's commerce service. The Steam Store row browses Steam's public storefront, and Steam still owns every purchase. |

### 5.3 Shell hunk D

`shell.cpp`, the `Action::Unavailable` case in `activate()` (replace the existing two-line body):

```cpp
    case Action::Unavailable:
        // A row this build cannot perform still says why. The Continue Watching row whose file is gone
        // uses its detail line; every row that carries its reason in `value` (the Network column's
        // unavailable rows, the PSN column, the TV/Video services row) shows that instead of the generic
        // sentence. Rows with neither keep the original line, unchanged.
        message_=entry.id.startsWith("continue:") && !entry.detail.isEmpty()
            ?entry.title+"\n\n"+entry.detail
            :!entry.value.isEmpty()?entry.title+"\n\n"+entry.value
            :entry.title+"\n\nThis feature is not available in the native recreation yet.";
        break;
```

`shell.cpp`, `resolveNetworkColumn()`'s `actionFor` (replace the whole lambda and add the assignment below
it; the strings are the table above, so the log line and the screen cannot disagree):

```cpp
    const auto actionFor=[](RowKind kind,const XmbmlRow& row)->std::pair<Action,QString> {
        // The four known rows carry the sentence the user sees when the row is opened. Keyed on the XMBML
        // row key so the wording survives a change in the row's item class (audit/SERVICES-BREADTH.md, 6.4).
        if(row.key==QStringLiteral("seg_manual"))
            return {Action::Unavailable,QStringLiteral("The console opened each game's manual in the PS3 "
                "Internet Browser, from Sony's servers. This build has no browser and no manual service, so "
                "there is no page to open.")};
        if(row.key==QStringLiteral("seg_browser"))
            return {Action::Unavailable,QStringLiteral("The PS3 Internet Browser is not part of this build, "
                "and a Network row carries no URL to hand to the PC browser.")};
        if(row.key==QStringLiteral("seg_kensaku"))
            return {Action::Unavailable,QStringLiteral("Internet Search was the PS3 browser's search page. "
                "This build has no browser; search on the PC instead.")};
        if(row.key==QStringLiteral("seg_premo"))
            return {Action::Unavailable,QStringLiteral("Remote Play needs a PS3 and a registered PSP or PS "
                "Vita. A PC has no equivalent, and the usual PC alternative would take over this machine, so "
                "this build does not offer it.")};
        switch(kind) {
        case RowKind::DownloadControl:
            return {Action::SteamDownloads,QStringLiteral("the console's download plugin lists its queue; "
                "this build lists Steam's own download queue from the manifests Steam writes "
                "(src/steam_downloads.cpp, audit/STEAM-DOWNLOADS.md)")};
        case RowKind::WebBrowser:
            return {Action::Unavailable,QStringLiteral("This row opens the PS3 Internet Browser, which this "
                "build does not have.")};
        case RowKind::ModuleAction:
            return {Action::Unavailable,QStringLiteral("This row runs a PS3 browser plugin (")+
                (row.moduleName.isEmpty()?QStringLiteral("unknown"):row.moduleName)+QLatin1Char(':')+
                (row.moduleAction.isEmpty()?QStringLiteral("unknown"):row.moduleAction)+
                QStringLiteral("), and this build has no PS3 browser.")};
        case RowKind::RemotePlay:
            return {Action::Unavailable,QStringLiteral("Remote Play needs a PS3 and a registered PSP or "
                "PS Vita; a PC has no equivalent in this build.")};
        case RowKind::DynamicProvider:
            return {Action::Unavailable,QStringLiteral("This row came from the console's ")+row.provider+
                QStringLiteral(":// provider at run time, and this build has no such provider.")};
        case RowKind::InGameExit:
        case RowKind::Unknown:
            break;
        }
        return {Action::Unavailable,QStringLiteral("The item class is not mapped to a native action.")};
    };
```

and where the decision is applied (after `entry.action=decision.first;`):

```cpp
        // The same sentence goes to the screen and to the catalogue dump: no reason is log-only.
        if(entry.action==Action::Unavailable)entry.value=decision.second;
```

`shell.cpp`, the constructor's `psn` and `tv` categories (replace those two entries; `item()`'s sixth
argument is `Entry::value`, the text the Unavailable case now shows):

```cpp
        {"tv","TV/Video Services","qgl/icontex","35n", /* normal-mapped like the others; tex_video is 46 px */
            {item("services","My Channels","tex_video","explore_plugin_full",Action::Unavailable,
                "My Channels listed the console's own TV and video services. This build lists media services "
                "from its content registry instead; nothing here is subscribed, signed in or purchased.")}},
        {"psn","PlayStation Network","xmb_plugin_normal","tex_psn", /* 128x128; explore_plugin_full has 46 */
            {item("signin","Sign In","tex_Sign_In","explore_plugin_full",Action::Unavailable,
                "There is no PlayStation Network sign-in in this build. PSN sign-in needs Sony's "
                "authentication service; the XMB does not ask for a password it cannot verify."),
             item("trophy","Trophy Collection","item_tex_trophy","explore_plugin_full",Action::Unavailable,
                "Trophy Collection (PSN)\n\nNo trophy backend exists here. PlayStation Network trophies need "
                "PSN sign-in and Sony's trophy service.\nThis build also has no local trophy data: RPCS3's "
                "dev_hdd0 has no trophy folder, and a PS3 title's earned trophies live in TROPUSR.DAT, which "
                "is encrypted with console keys the XMB does not use.\nSteam's own achievements are a "
                "different system and are not shown either."),
             item("store","PlayStation Store","item_tex_ps_store","explore_plugin_full",Action::Unavailable,
                "This build cannot buy anything. The console store needed PSN sign-in and Sony's commerce "
                "service. The Steam Store row browses Steam's public storefront, and Steam still owns every "
                "purchase.")}},
```

Because `Entry::value` is already part of the catalogue dump, the new wording is dump-checkable without
reading pixels (`XMB_DUMP_ENTRIES`).

---

## 6. Disc and media services (6.5)

### 6.1 What the build actually has

- The Game column's disc titles are RPCS3's copies of a disc's files (`games.yml`), started through RPCS3.
  There is no optical drive integration, no disc detection, no disc menu and no BD-Java path.
- The Video column plays files and Jellyfin; it has no disc row at all.
- Video Settings lists Repeat, Shuffle and Volume only; the firmware's BD/DVD output rows are absent rather
  than stubbed (verified in `settings_menu.cpp` on 2026-10-27).
- TV/Video Services has one row, "My Channels", which was the console's own service list.

### 6.2 Exact wording

| Where | Text |
|---|---|
| A disc-based game row (Game column) | Disc-based title: this build starts a copy of the disc's files through RPCS3 and has no Blu-ray drive integration. |
| Its Information page (longer form, appended to the existing details) | Disc playback is not available: this build has no Blu-ray or DVD drive integration and no disc menu. The title starts from a copy of the disc's files on this PC. |
| TV/Video Services > My Channels | My Channels listed the console's own TV and video services. This build lists media services from its content registry instead; nothing here is subscribed, signed in or purchased. |
| Video Settings | (no change) The console's BD/DVD output rows do not exist in this build rather than pretending to be settings. |
| Media service rows | (no change) A web registration says "Service app - Opens in your browser"; Jellyfin is the native one. Neither claims a purchase or a subscription. |

### 6.3 Shell hunk E

`shell.cpp`, `addInstalledApps()`, in the `add` lambda (after the `details` string and before the entry is
created):

```cpp
            // A disc copy is RPCS3's copy of the disc's files, not a drive: the row's own line says so,
            // because this build has no Blu-ray drive integration (audit/SERVICES-BREADTH.md, 6.5).
            const QString discNote=discCopy
                ?QStringLiteral("\nDisc playback is not available: this build has no Blu-ray or DVD drive "
                    "integration and no disc menu. The title starts from a copy of the disc's files on this PC.")
                :QString{};
```

then use it in both places:

```cpp
            const QString details=sfo.value("TITLE")+"\n\n"+id+"  "+sfo.value("APP_VER",sfo.value("VERSION"))+discNote;
            auto entry=item("app:"+id+":"+QFileInfo(folder).fileName(),sfo.value("TITLE"),
                QFileInfo::exists(icon)?icon:QString("game_tex_default_ps3"),
                QFileInfo::exists(icon)?QString("file"):QString("explore_plugin_full"),Action::Information,
                details+(emulator?"\nThis title has no EBOOT.BIN to start."
                                 :"\nNo RPCS3 executable was found next to dev_hdd0, so it cannot be started."));
            if(discCopy)entry.detail=QStringLiteral("Disc-based title: this build starts a copy of the disc's "
                "files through RPCS3 and has no Blu-ray drive integration.");
```

The `detail` line is drawn under the focused row and is also the `Detail:` line of the Information page, so
the sentence is visible both without pressing anything and from the panel.

---

## 7. Gates and evidence for this pass

Build (the mandated command, last run after the last source edit):

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-steam6
```

Exit 0. `steam_accounts.cpp`, `steam_library.cpp` and `steam_friends.cpp` compile with no warnings at
`/W4 /permissive- /utf-8` (the warnings in the log belong to `view.cpp` and `main.cpp`). One build attempt
between two of those runs failed in `jellyfin_shell.cpp(122)`, another agent's in-flight file; the retry
immediately after was green and that file is untouched by this pass.

Layer gate (the mandated command):

```
& "$env:USERPROFILE\.local\bin\bun.exe" work\ab-layers.ts --all --exe C:/dev/out/native-steam6/re-ps3-native.exe
```

- stored baseline pair: 6/6 layers PASS (frame +0.00 dB, SSIM +0.0000; the other five likewise);
- canonical capture: 6/6 PASS (frame 18.28 vs 18.02, SSIM -0.0016; top background -0.01 dB; category icon
  row -0.16 dB; item label row, clock text and info bar +0.00);
- named states: 4/4 PASS, every region `inf / 1.0000` (`steam-all-games`, `steam-store-sections`,
  `steam-not-installed`, and the root state), row counts unchanged (11 Game rows, 275 Not Installed rows);
- `whole set: PASS`, exit 0.

Honest note on one transient run: the first `--all` attempt aborted while resolving the
`steam-not-installed` path, because that capture's log showed the Store row's options panel open
(`shell: options opened ... entry= steam-store`) instead of the All Games page. No source in this pass
touches input or the Game column; an immediate `--states` re-run passed 4/4 and the next `--all` passed
whole-set, so the aborted capture was an input artifact, not a regression. The capture's evidence is kept
at the temp path the harness printed.

CLI/driver evidence (all from `C:\dev\out\steam6-harness`, a temporary build outside the repository; source
and synthetic logs stay there):

| Command | Result |
|---|---|
| `steam6-driver.exe --plan` | 3 prompts, 25 assertions, `failures: 0`; dry-run commands exactly `steam.exe -shutdown` then `steam.exe`, `executed=0` |
| `steam6-driver.exe --synthetic <root>` | 17 assertions, `failures: 0` (delivery, wrong id, snapshot, rejection, rotation, missing log, caps, argument validation) |
| `steam6-driver.exe --verify 999999999 499462` | `delivered: 1`, `rejected: 0`, the real 2026-10-26 command line |
| `steam6-driver.exe --stop 999999999` | handoff started; no log line because the client was signed out (see 2.4) - reported, not hidden |
| `steam6-driver.exe --avatars <root>` | 16 assertions, `failures: 0` |
| `steam6-driver.exe --avatar-fetch <root>` | real avatar 64x64, 8,194-byte cache file, cache reuse suppresses the fetch |

## 8. Files

- `src/steam_accounts.h`, `src/steam_accounts.cpp` - `SteamSwitchStep`, `SteamSwitchPrompt`,
  `steamSwitchPlanFor()`, `SteamSwitchConfirmation`, `SteamSwitchDryRun`/`steamSwitchDryRun()`.
- `src/steam_library.h`, `src/steam_library.cpp` - validated `steamStopArguments()`, hardened
  `steamStopApp()`, `steamConsoleLogTail()`, `steamConsoleLogSize()`, `steamVerifyStop()`.
- `src/steam_friends.h`, `src/steam_friends.cpp` - `SteamFriendAvatars` (`cachePath`, `cachedAvatars`,
  `avatarUrlForProfileXml`, `refresh`).
- This file.
- A dated section in [STEAM-INTEGRATION.md](STEAM-INTEGRATION.md).

Not touched: `shell.cpp`/`shell.h`, `view.cpp`/`view.h`, `main.cpp`, `settings_menu.cpp`,
`preferences.cpp`, `jellyfin_*`, `CMakeLists.txt`, the harness, and anything under `C:\dev\rpcs3`.
