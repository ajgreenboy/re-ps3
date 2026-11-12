#include "steam_accounts.h"
#include "steam_vdf.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <algorithm>
#include <utility>

namespace xmb {
namespace {

// Bounds for Steam's loginusers.vdf: normally a few hundred bytes, but the parser
// must stay bounded no matter what is on disk.
constexpr qsizetype MaximumLoginUsersBytes = 1024 * 1024;
constexpr qsizetype MaximumTokenBytes = 4096;
constexpr qsizetype MaximumTokenCount = 20000;
constexpr qsizetype MaximumEntries = 4096;
constexpr int MaximumDepth = 8;
constexpr qsizetype MaximumAccounts = 64;
constexpr qsizetype MaximumAccountNameCharacters = 64;
constexpr qsizetype MaximumPersonaCharacters = 256;
constexpr qulonglong SteamIdBase = 76561197960265728ULL;
constexpr qulonglong MaximumAccountId = 0xffffffffULL;
constexpr qint64 MaximumLoginTimestamp = 4102444800LL; // 2100-01-01 UTC

using KvEntry = steamvdf::Entry;
using KvDocument = steamvdf::Document;

// The KeyValues dialect of loginusers.vdf, read by the shared parser (steam_vdf.h): the leading
// UTF-8 BOM is skipped, quoted braces are ordinary strings, and the limits are the ones above.
KvDocument parseKeyValues(const QByteArray& bytes) {
    steamvdf::Options options;
    options.skipBom = true;
    options.quotedBracesAreStructural = false;
    options.maxTokenBytes = MaximumTokenBytes;
    options.maxTokens = MaximumTokenCount;
    options.maxEntries = MaximumEntries;
    options.maxDepth = MaximumDepth;
    return steamvdf::parse(bytes, options);
}

bool readBounded(const QString& path, qsizetype maximum, QByteArray* bytes, QString* detail) {
    return steamvdf::readBounded(path, maximum, bytes, detail, true);
}

// Keys are matched exactly (case-sensitively) in this file.
const KvEntry* findEntry(const QVector<KvEntry>& entries, const QString& key) {
    return steamvdf::findEntry(entries, key, Qt::CaseSensitive);
}

QString stringValue(const QVector<KvEntry>& entries, const QString& key) {
    return steamvdf::stringValue(entries, key, Qt::CaseSensitive);
}

bool truthy(const QString& value) {
    const QString trimmed = value.trimmed();
    return trimmed == QStringLiteral("1") ||
           trimmed.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

// Display-safe text: control and line/paragraph separators are dropped (a persona
// name is rendered by the XMB), then the value is trimmed and truncated.
QString cleanedText(const QString& value, qsizetype maximumCharacters) {
    QString result;
    result.reserve(std::min<qsizetype>(value.size(), maximumCharacters));
    for (const QChar character : value) {
        const auto category = character.category();
        if (category == QChar::Other_Control || category == QChar::Separator_Line ||
            category == QChar::Separator_Paragraph)
            continue;
        if (result.size() >= maximumCharacters) break;
        result.append(character);
    }
    return result.trimmed();
}

bool parseSteamId64(const QString& key, quint64* steamId) {
    if (key.isEmpty() || key.size() > 20) return false;
    for (const QChar character : key) {
        if (!character.isDigit() || character.unicode() > 0x7f) return false;
    }
    bool valid = false;
    const qulonglong parsed = key.toULongLong(&valid, 10);
    if (!valid || parsed < SteamIdBase) return false;
    const qulonglong accountId = parsed - SteamIdBase;
    if (accountId == 0 || accountId > MaximumAccountId) return false;
    if (steamId) *steamId = parsed;
    return true;
}

// loginusers.vdf uses "timestamp" on current clients and "Timestamp" on older
// ones; both are accepted.
qint64 loginTimestamp(const QVector<KvEntry>& entries) {
    const QStringList keys{QStringLiteral("Timestamp"), QStringLiteral("timestamp")};
    for (const QString& key : keys) {
        const QString value = stringValue(entries, key).trimmed();
        if (value.isEmpty()) continue;
        bool valid = false;
        const qlonglong parsed = value.toLongLong(&valid, 10);
        if (valid && parsed > 0 && parsed <= MaximumLoginTimestamp) return parsed;
    }
    return 0;
}

QString canonicalDirectory(const QString& path) {
    if (path.trimmed().isEmpty()) return {};
    const QFileInfo info(QDir::cleanPath(path.trimmed()));
    return info.isDir() ? QDir::fromNativeSeparators(info.canonicalFilePath()) : QString{};
}

QStringList steamRootCandidates(const QString& explicitRoot) {
    if (!explicitRoot.trimmed().isEmpty()) return {explicitRoot.trimmed()};
    const QString environmentRoot = qEnvironmentVariable("XMB_STEAM_ROOT").trimmed();
    if (!environmentRoot.isEmpty()) return {environmentRoot};
    QStringList candidates;
#ifdef Q_OS_WIN
    QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    if (steam.status() == QSettings::NoError)
        candidates.append(steam.value(QStringLiteral("SteamPath")).toString().trimmed());
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)").trimmed();
    if (!programFilesX86.isEmpty())
        candidates.append(QDir(programFilesX86).filePath(QStringLiteral("Steam")));
    const QString programFiles = qEnvironmentVariable("ProgramFiles").trimmed();
    if (!programFiles.isEmpty()) candidates.append(QDir(programFiles).filePath(QStringLiteral("Steam")));
#else
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!home.isEmpty()) {
        candidates.append(QDir(home).filePath(QStringLiteral(".steam/steam")));
        candidates.append(QDir(home).filePath(QStringLiteral(".local/share/Steam")));
    }
#endif
    return candidates;
}

// A Steam client root always carries a config directory (loginusers.vdf lives in
// it); that rejects a library folder or an unrelated directory passed by mistake.
QString resolveSteamRoot(const QString& explicitRoot, QString* error) {
    for (const QString& candidate : steamRootCandidates(explicitRoot)) {
        const QString canonical = canonicalDirectory(candidate);
        if (canonical.isEmpty()) continue;
        if (!QFileInfo(QDir(canonical).filePath(QStringLiteral("config"))).isDir()) continue;
        return canonical;
    }
    if (error) {
        *error = QStringLiteral(
            "Steam was not found. Looked for XMB_STEAM_ROOT, the registry Steam path and the default "
            "installation folders.");
    }
    return {};
}

// What the running client records about itself. All of this is read-only; the
// values are the client's own claims and can be stale after the client exits.
struct RegistryState {
    bool rootMatches = false;
    bool activeProcessPresent = false;
    qulonglong signedInSteamId = 0;
    QString autoLoginUser;
    QString steamExe;
};

RegistryState readRegistryState(const QString& root) {
    RegistryState state;
#ifdef Q_OS_WIN
    QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    if (steam.status() != QSettings::NoError) return state;
    const QString registryRoot = canonicalDirectory(steam.value(QStringLiteral("SteamPath")).toString());
    const QString canonicalRoot = canonicalDirectory(root);
    if (registryRoot.isEmpty() || canonicalRoot.isEmpty() ||
        registryRoot.compare(canonicalRoot, Qt::CaseInsensitive) != 0)
        return state;
    state.rootMatches = true;
    state.steamExe = steam.value(QStringLiteral("SteamExe")).toString().trimmed();
    state.autoLoginUser = steam.value(QStringLiteral("AutoLoginUser")).toString().trimmed();
    QSettings activeProcess(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam\\ActiveProcess"),
                            QSettings::NativeFormat);
    if (activeProcess.status() == QSettings::NoError) {
        state.activeProcessPresent = activeProcess.contains(QStringLiteral("ActiveUser"));
        if (state.activeProcessPresent) {
            bool valid = false;
            const qulonglong accountId =
                activeProcess.value(QStringLiteral("ActiveUser")).toULongLong(&valid);
            if (valid && accountId > 0 && accountId <= MaximumAccountId)
                state.signedInSteamId = SteamIdBase + accountId;
        }
    }
#else
    Q_UNUSED(root);
#endif
    return state;
}

QString steamExecutable(const QString& root, const RegistryState& registry) {
    const QStringList candidates{QDir(root).filePath(QStringLiteral("steam.exe")),
                                 QDir(root).filePath(QStringLiteral("steam"))};
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile()) return QDir::fromNativeSeparators(info.absoluteFilePath());
    }
#ifdef Q_OS_WIN
    if (registry.rootMatches && !registry.steamExe.isEmpty()) {
        const QFileInfo info(registry.steamExe);
        if (info.isFile()) return QDir::fromNativeSeparators(info.absoluteFilePath());
    }
#else
    Q_UNUSED(registry);
#endif
    return {};
}

struct ParsedAccount {
    SteamAccount account;
    bool legacyMostRecent = false;
};

// Steam's own record decides the active marker; nothing is guessed when the
// client reports a signed-out state.
void markActiveAccount(QVector<ParsedAccount>& parsed, const RegistryState& registry) {
    if (parsed.isEmpty()) return;
    if (registry.signedInSteamId != 0) {
        const QString signedInId = QString::number(registry.signedInSteamId);
        for (auto& entry : parsed) {
            if (entry.account.steamId64 == signedInId) entry.account.active = true;
        }
        // A signed-in account that loginusers.vdf does not remember stays unlisted.
        return;
    }
    if (registry.activeProcessPresent) return; // the client reports that nobody is signed in
    if (registry.rootMatches && !registry.autoLoginUser.isEmpty()) {
        for (auto& entry : parsed) {
            if (entry.account.accountName.compare(registry.autoLoginUser, Qt::CaseInsensitive) == 0) {
                entry.account.active = true;
                return;
            }
        }
    }
    // The file itself can name the auto-sign-in account (AutoLogin on current
    // clients, AllowAutoLogin on older ones). Prefer it when the registry has
    // nothing for this installation, exactly as documented in the header.
    int autoLoginIndex = -1;
    int autoLoginCount = 0;
    for (qsizetype i = 0; i < parsed.size(); ++i) {
        if (!parsed.at(i).account.autoLogin) continue;
        ++autoLoginCount;
        autoLoginIndex = static_cast<int>(i);
    }
    if (autoLoginCount == 1) {
        parsed[autoLoginIndex].account.active = true;
        return;
    }
    // Legacy clients store the last-used account as MostRecent=1 in the same file.
    int mostRecentIndex = -1;
    for (qsizetype i = 0; i < parsed.size(); ++i) {
        if (!parsed.at(i).legacyMostRecent) continue;
        if (mostRecentIndex < 0 ||
            parsed.at(i).account.steamId64.toULongLong() >
                parsed.at(mostRecentIndex).account.steamId64.toULongLong()) {
            mostRecentIndex = static_cast<int>(i);
        }
    }
    if (mostRecentIndex >= 0) {
        parsed[mostRecentIndex].account.active = true;
        return;
    }
    if (parsed.size() == 1) parsed[0].account.active = true; // one remembered account: unambiguous
}

void sortAccounts(QVector<SteamAccount>& accounts) {
    std::stable_sort(accounts.begin(), accounts.end(),
                     [](const SteamAccount& left, const SteamAccount& right) {
                         if (left.active != right.active) return left.active;
                         if (left.lastLoginUnix != right.lastLoginUnix)
                             return left.lastLoginUnix > right.lastLoginUnix;
                         return left.steamId64.toULongLong() < right.steamId64.toULongLong();
                     });
}

} // namespace

QVector<SteamAccount> scanSteamAccounts(const QString& steamRoot, QString* error) {
    if (error) error->clear();
    const QString root = resolveSteamRoot(steamRoot, error);
    if (root.isEmpty()) return {};

    const QString path = QDir(root).filePath(QStringLiteral("config/loginusers.vdf"));
    if (!QFileInfo(path).isFile()) return {}; // Steam is installed; no account is remembered

    QByteArray bytes;
    QString detail;
    if (!readBounded(path, MaximumLoginUsersBytes, &bytes, &detail)) {
        if (error) *error = QStringLiteral("Steam's account file could not be read: %1").arg(detail);
        return {};
    }
    const KvDocument document = parseKeyValues(bytes);
    if (!document.error.isEmpty()) {
        if (error)
            *error = QStringLiteral("Steam's account file could not be parsed: %1").arg(document.error);
        return {};
    }
    const KvEntry* users = findEntry(document.entries, QStringLiteral("users"));
    if (!users || !users->block) {
        if (error) *error = QStringLiteral("Steam's account file has no users block.");
        return {};
    }

    QVector<ParsedAccount> parsed;
    QSet<QString> ids;
    QSet<QString> names;
    QString limitError;
    for (const auto& user : users->children) {
        if (!user.block) continue;
        quint64 numericId = 0;
        if (!parseSteamId64(user.key, &numericId)) continue; // not an account block
        const QString steamId = QString::number(numericId);
        if (ids.contains(steamId)) continue;

        SteamAccount account;
        account.steamId64 = steamId;
        account.accountName =
            cleanedText(stringValue(user.children, QStringLiteral("AccountName")),
                        MaximumAccountNameCharacters);
        if (account.accountName.isEmpty()) continue;
        const QString foldedName = account.accountName.toCaseFolded();
        if (names.contains(foldedName)) continue;
        if (parsed.size() >= MaximumAccounts) {
            limitError = QStringLiteral(
                             "Steam lists more accounts than this build supports (%1); only the first %1 "
                             "are shown.")
                             .arg(static_cast<qlonglong>(MaximumAccounts));
            break;
        }

        account.personaName =
            cleanedText(stringValue(user.children, QStringLiteral("PersonaName")),
                        MaximumPersonaCharacters);
        if (account.personaName.isEmpty()) account.personaName = account.accountName;
        account.rememberPassword =
            truthy(stringValue(user.children, QStringLiteral("RememberPassword")));
        const QString autoLoginValue = stringValue(user.children, QStringLiteral("AutoLogin"));
        account.autoLogin = autoLoginValue.isEmpty()
                                ? truthy(stringValue(user.children, QStringLiteral("AllowAutoLogin")))
                                : truthy(autoLoginValue);
        account.lastLoginUnix = loginTimestamp(user.children);

        const qulonglong accountId = numericId - SteamIdBase;
        const QString localConfig = QDir(root).filePath(
            QStringLiteral("userdata/%1/config/localconfig.vdf").arg(accountId));
        // Local data is evidence that the account has been used on this machine; it
        // is not proof of a valid or current session.
        account.hasUserdata = QFileInfo(localConfig).isFile();

        ParsedAccount entry;
        entry.account = std::move(account);
        entry.legacyMostRecent =
            truthy(stringValue(user.children, QStringLiteral("MostRecent")));
        parsed.append(std::move(entry));
        ids.insert(steamId);
        names.insert(foldedName);
    }

    markActiveAccount(parsed, readRegistryState(root));

    QVector<SteamAccount> accounts;
    accounts.reserve(parsed.size());
    for (auto& entry : parsed) accounts.append(std::move(entry.account));
    sortAccounts(accounts);
    if (error && !limitError.isEmpty()) *error = limitError;
    return accounts;
}

QStringList steamShutdownArguments() {
    // The client's own exit request, the same string desktop launchers use to close
    // it. The caller should pass it only while Steam is running: given to a stopped
    // client it starts the client briefly and closes it again.
    return {QStringLiteral("-shutdown")};
}

QStringList steamRelaunchArguments() {
    // Deliberately empty. The client decides whether to auto-sign-in a remembered
    // account or show its sign-in window. The XMB never passes -login, an account
    // name, a password or a token: Valve exposes no supported switch command, and
    // an undocumented credential flag would make the XMB claim authentication it
    // does not own.
    return {};
}

SteamSwitchPlan steamAccountSwitchPlan(const SteamAccount& account, const QString& steamRoot,
                                       QString* error) {
    SteamSwitchPlan plan;
    if (error) error->clear();
    const QString root = resolveSteamRoot(steamRoot, error);
    if (root.isEmpty()) {
        plan.explanation =
            QStringLiteral("Steam was not found, so the XMB cannot hand an account switch to it.");
        return plan;
    }
    const RegistryState registry = readRegistryState(root);
    plan.clientSignedIn = registry.signedInSteamId != 0;
    plan.steamExe = steamExecutable(root, registry);
    plan.shutdownArguments = steamShutdownArguments();
    plan.relaunchArguments = steamRelaunchArguments();
    plan.alreadyActive = !account.steamId64.isEmpty() && account.active;

    if (plan.steamExe.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                "The Steam client executable was not found in the selected Steam installation.");
        plan.explanation =
            QStringLiteral("Steam's executable was not found, so there is nothing to hand the switch to.");
        return plan;
    }
    if (account.steamId64.isEmpty() || account.accountName.isEmpty()) {
        if (error) *error = QStringLiteral("No Steam account was selected.");
        plan.explanation = QStringLiteral("No Steam account is selected.");
        return plan;
    }
    plan.possible = true;

    const QString display =
        account.personaName.isEmpty() ? account.accountName : account.personaName;
    if (plan.alreadyActive) {
        plan.explanation = QStringLiteral(
                               "Steam's own record already names %1 as the active account. To use "
                               "another one, change it inside Steam's own account menu; the XMB cannot "
                               "sign in.")
                               .arg(display);
    } else if (!account.hasUserdata) {
        plan.explanation = QStringLiteral(
                               "The XMB cannot sign in to Steam. Steam will be asked to close, then started "
                               "again; sign in to %1 in Steam's own window. This account has no local "
                               "userdata yet, so Steam creates it after the first signed-in session. The "
                               "XMB does not store credentials.")
                               .arg(display);
    } else {
        plan.explanation = QStringLiteral(
                               "The XMB cannot sign in to Steam. Steam will be asked to close, then started "
                               "again; sign in to %1 in Steam's own window. Its local userdata is used only "
                               "after Steam signs in. The XMB does not store credentials.")
                               .arg(display);
    }
    return plan;
}

namespace {

QString accountLabel(const SteamAccount& account) {
    if (!account.personaName.trimmed().isEmpty()) return account.personaName.trimmed();
    return account.accountName.trimmed();
}

// How a user would type it, and the same shape the shell logs. Windows quoting only: the
// executable path gets quotes when it contains a space, arguments are passed unchanged.
QString switchCommandLine(const QString& executable, const QStringList& arguments) {
    QString line = executable.contains(QLatin1Char(' '))
                       ? QLatin1Char('"') + executable + QLatin1Char('"')
                       : executable;
    for (const QString& argument : arguments) line += QLatin1Char(' ') + argument;
    return line;
}

QString noSignInSentence() {
    return QStringLiteral(
        "The XMB cannot sign in to Steam and never stores a password, token or session file.");
}

} // namespace

SteamSwitchPrompt steamSwitchPlanFor(const SteamAccount& account, const QString& steamRoot,
                                     QString* error) {
    SteamSwitchPrompt prompt;
    QString planError;
    const SteamSwitchPlan plan = steamAccountSwitchPlan(account, steamRoot, &planError);
    prompt.possible = plan.possible;
    prompt.alreadyActive = plan.alreadyActive;
    prompt.clientSignedIn = plan.clientSignedIn;
    prompt.steamExe = plan.steamExe;
    prompt.accountLabel = accountLabel(account);
    prompt.shutdownArguments = plan.shutdownArguments;
    prompt.relaunchArguments = plan.relaunchArguments;
    // The promise every step repeats: the XMB's own state is not part of the restart. It is true
    // because the handoff only starts and stops the Steam client process.
    prompt.xmbNote = QStringLiteral("Steam must restart; unsaved XMB state is unaffected.");
    prompt.outcomeText = QStringLiteral(
        "Steam is starting. The account is only known once Steam's own record changes; the XMB "
        "re-reads that record and never reports a sign-in itself.");

    if (!plan.possible) {
        if (error && !planError.isEmpty()) *error = planError;
        prompt.blocked.heading = QStringLiteral("Steam Account");
        prompt.blocked.body = plan.explanation + QStringLiteral("\n\nNothing was started.");
        return prompt;
    }
    if (plan.alreadyActive) {
        // Nothing to hand off: the client already names this account. The step still explains why,
        // so the row never looks like a dead end.
        prompt.blocked.heading = QStringLiteral("Steam Account");
        prompt.blocked.body = QStringLiteral(
                                  "Steam's own record already names %1 as the active account. "
                                  "Nothing was started.\n\n"
                                  "To use another account, change it in Steam's own account menu; "
                                  "the XMB cannot sign in.")
                                  .arg(prompt.accountLabel);
        return prompt;
    }

    SteamSwitchStep close;
    close.heading = QStringLiteral("Switch Steam Account");
    close.body = prompt.xmbNote + QStringLiteral("\n\n") +
                 QStringLiteral("Steam will be asked to close. ") + noSignInSentence() +
                 QStringLiteral("\n\nNext: start Steam again and sign in as %1 in Steam's own window.")
                     .arg(prompt.accountLabel);
    close.confirmLabel = QStringLiteral("Close Steam");
    close.cancelLabel = QStringLiteral("Cancel");

    SteamSwitchStep relaunch;
    relaunch.heading = QStringLiteral("Start Steam Again");
    relaunch.body = QStringLiteral(
                        "Steam is closing. Start it again to reach Steam's own sign-in window.\n\n"
                        "Sign in as %1 there. Steam asks for the password and Steam Guard, and the "
                        "XMB cannot do either for you.\n\n"
                        "Steam picks the account it signs in, so the XMB does not report a switch "
                        "as done.")
                        .arg(prompt.accountLabel);
    relaunch.confirmLabel = QStringLiteral("Start Steam");
    relaunch.cancelLabel = QStringLiteral("Not Now");

    prompt.steps.append(close);
    prompt.steps.append(relaunch);
    return prompt;
}

bool SteamSwitchConfirmation::confirm() {
    switch (stage) {
    case Stage::ConfirmClose:
        stage = Stage::ConfirmRelaunch;
        accepted = 1;
        return true;
    case Stage::ConfirmRelaunch:
        stage = Stage::Ready;
        accepted = 2;
        return true;
    case Stage::Blocked:
    case Stage::Ready:
    case Stage::Cancelled:
        break;
    }
    // Blocked, already accepted, or cancelled: nothing advances, so no command can be run twice
    // or after a cancellation.
    return false;
}

void SteamSwitchConfirmation::cancel() {
    stage = Stage::Cancelled;
    accepted = 0;
}

SteamSwitchDryRun steamSwitchDryRun(const SteamAccount& account, const QString& steamRoot,
                                    QString* error) {
    SteamSwitchDryRun run;
    QString planError;
    const SteamSwitchPrompt prompt = steamSwitchPlanFor(account, steamRoot, &planError);
    run.blocked = prompt.blocked;
    run.possible = prompt.possible && prompt.steps.size() == 2;
    if (!run.possible) {
        run.error = planError.isEmpty()
                        ? (prompt.blocked.body.isEmpty() ? QStringLiteral("no switch is possible")
                                                         : prompt.blocked.body)
                        : planError;
        run.transcript << prompt.blocked.heading << prompt.blocked.body;
        if (error) *error = run.error;
        return run;
    }
    if (error) error->clear();

    SteamSwitchConfirmation confirmation;
    confirmation.stage = SteamSwitchConfirmation::Stage::ConfirmClose;
    const SteamSwitchStep& close = prompt.steps.at(0);
    const SteamSwitchStep& relaunch = prompt.steps.at(1);

    run.transcript << close.heading << close.body
                   << QStringLiteral("[%1]").arg(close.confirmLabel);
    if (!confirmation.confirm()) {
        run.possible = false;
        run.error = QStringLiteral("the confirmation model refused the first step");
        if (error) *error = run.error;
        return run;
    }
    const QString shutdown = switchCommandLine(prompt.steamExe, prompt.shutdownArguments);
    run.commands << shutdown;

    run.transcript << QStringLiteral("step 2 after: %1").arg(shutdown) << relaunch.heading
                   << relaunch.body << QStringLiteral("[%1]").arg(relaunch.confirmLabel);
    if (!confirmation.confirm()) {
        run.possible = false;
        run.error = QStringLiteral("the confirmation model refused the second step");
        if (error) *error = run.error;
        return run;
    }
    run.commands << switchCommandLine(prompt.steamExe, prompt.relaunchArguments);
    run.transcript << prompt.outcomeText;
    // executed stays false: this function is the dry run. The shell owns the real handoff.
    return run;
}

} // namespace xmb
