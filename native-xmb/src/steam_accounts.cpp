#include "steam_accounts.h"

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

struct KvEntry {
    QString key;
    QString value;
    QVector<KvEntry> children;
    bool block = false;
};

struct KvDocument {
    QVector<KvEntry> entries;
    QString error;
};

enum class TokenKind { Word, BraceOpen, BraceClose };

struct Token {
    QString text;
    TokenKind kind = TokenKind::Word;
};

bool readBounded(const QString& path, qsizetype maximum, QByteArray* bytes, QString* detail) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (detail) *detail = file.errorString();
        return false;
    }
    const qint64 size = file.size();
    if (size < 0 || size > maximum) {
        if (detail) *detail = QStringLiteral("the file exceeds the supported size");
        return false;
    }
    *bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError || bytes->size() > maximum || !file.atEnd()) {
        if (detail) {
            *detail = file.errorString().isEmpty() ? QStringLiteral("the file could not be read completely")
                                                   : file.errorString();
        }
        bytes->clear();
        return false;
    }
    return true;
}

// The same KeyValues dialect Steam's own files use; structurally the parser in
// steam_library.cpp, kept local so this module has no dependency on that file.
class KeyValuesParser final {
public:
    explicit KeyValuesParser(QByteArray input) : input_(std::move(input)) {}

    KvDocument parse() {
        KvDocument document;
        if (!tokenize()) {
            document.error = error_;
            return document;
        }
        qsizetype entries = 0;
        if (!parseEntries(0, document.entries, entries) || position_ != tokens_.size()) {
            document.entries.clear();
            document.error = error_.isEmpty() ? QStringLiteral("unexpected trailing KeyValues data") : error_;
        }
        return document;
    }

private:
    bool tokenize() {
        qsizetype i = 0;
        if (input_.size() >= 3 && static_cast<unsigned char>(input_.at(0)) == 0xef &&
            static_cast<unsigned char>(input_.at(1)) == 0xbb &&
            static_cast<unsigned char>(input_.at(2)) == 0xbf)
            i = 3; // UTF-8 BOM
        while (i < input_.size()) {
            const auto c = static_cast<unsigned char>(input_.at(i));
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < input_.size() && input_.at(i + 1) == '/') {
                i += 2;
                while (i < input_.size() && input_.at(i) != '\n') ++i;
                continue;
            }
            if (c == '{' || c == '}') {
                Token token;
                token.kind = c == '{' ? TokenKind::BraceOpen : TokenKind::BraceClose;
                tokens_.append(std::move(token));
                if (tokens_.size() > MaximumTokenCount) {
                    error_ = QStringLiteral("KeyValues token count exceeds the limit");
                    return false;
                }
                ++i;
                continue;
            }
            if (c == '"') {
                ++i;
                QByteArray text;
                bool closed = false;
                while (i < input_.size()) {
                    const char current = input_.at(i++);
                    if (current == '"') {
                        closed = true;
                        break;
                    }
                    if (current == '\\' && i < input_.size()) {
                        const char next = input_.at(i);
                        if (next == '"' || next == '\\') {
                            text.append(next);
                            ++i;
                        } else {
                            text.append(current);
                        }
                    } else {
                        text.append(current);
                    }
                    if (text.size() > MaximumTokenBytes) {
                        error_ = QStringLiteral("KeyValues token exceeds the limit");
                        return false;
                    }
                }
                if (!closed) {
                    error_ = QStringLiteral("unterminated KeyValues string");
                    return false;
                }
                Token token;
                token.text = QString::fromUtf8(text);
                tokens_.append(std::move(token));
            } else {
                const qsizetype start = i;
                while (i < input_.size()) {
                    const auto current = static_cast<unsigned char>(input_.at(i));
                    if (current == ' ' || current == '\t' || current == '\r' || current == '\n' ||
                        current == '\v' || current == '\f' || current == '{' || current == '}')
                        break;
                    if (current == '/' && i + 1 < input_.size() && input_.at(i + 1) == '/') break;
                    ++i;
                }
                if (start == i) {
                    error_ = QStringLiteral("unsupported character in KeyValues file");
                    return false;
                }
                const QByteArray text = input_.mid(start, i - start);
                if (text.size() > MaximumTokenBytes) {
                    error_ = QStringLiteral("KeyValues token exceeds the limit");
                    return false;
                }
                Token token;
                token.text = QString::fromUtf8(text);
                tokens_.append(std::move(token));
            }
            if (tokens_.size() > MaximumTokenCount) {
                error_ = QStringLiteral("KeyValues token count exceeds the limit");
                return false;
            }
        }
        return true;
    }

    bool parseEntries(int depth, QVector<KvEntry>& entries, qsizetype& entriesUsed) {
        while (position_ < tokens_.size()) {
            const Token& token = tokens_.at(position_);
            if (token.kind == TokenKind::BraceClose) {
                if (depth == 0) {
                    error_ = QStringLiteral("unexpected closing brace");
                    return false;
                }
                return true; // the caller consumes the closing brace
            }
            if (token.kind == TokenKind::BraceOpen) {
                error_ = QStringLiteral("missing KeyValues key");
                return false;
            }
            const QString key = token.text;
            ++position_;
            if (position_ >= tokens_.size()) {
                error_ = QStringLiteral("missing KeyValues value");
                return false;
            }
            const Token& value = tokens_.at(position_);
            if (value.kind == TokenKind::BraceOpen) {
                ++position_;
                if (depth + 1 > MaximumDepth) {
                    error_ = QStringLiteral("KeyValues nesting exceeds the limit");
                    return false;
                }
                KvEntry entry;
                entry.key = key;
                entry.block = true;
                if (!parseEntries(depth + 1, entry.children, entriesUsed)) return false;
                if (position_ >= tokens_.size() || tokens_.at(position_).kind != TokenKind::BraceClose) {
                    error_ = QStringLiteral("unterminated KeyValues block");
                    return false;
                }
                ++position_;
                if (++entriesUsed > MaximumEntries) {
                    error_ = QStringLiteral("KeyValues entry count exceeds the limit");
                    return false;
                }
                entries.append(std::move(entry));
            } else if (value.kind == TokenKind::Word) {
                KvEntry entry;
                entry.key = key;
                entry.value = value.text;
                ++position_;
                if (++entriesUsed > MaximumEntries) {
                    error_ = QStringLiteral("KeyValues entry count exceeds the limit");
                    return false;
                }
                entries.append(std::move(entry));
            } else {
                error_ = QStringLiteral("missing KeyValues value");
                return false;
            }
        }
        if (depth != 0) {
            error_ = QStringLiteral("unterminated KeyValues block");
            return false;
        }
        return true;
    }

    QByteArray input_;
    QVector<Token> tokens_;
    qsizetype position_ = 0;
    QString error_;
};

const KvEntry* findEntry(const QVector<KvEntry>& entries, const QString& key) {
    for (const auto& entry : entries)
        if (entry.key == key) return &entry;
    return nullptr;
}

QString stringValue(const QVector<KvEntry>& entries, const QString& key) {
    const auto* entry = findEntry(entries, key);
    return entry && !entry->block ? entry->value : QString{};
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
    const KvDocument document = KeyValuesParser(std::move(bytes)).parse();
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

} // namespace xmb
