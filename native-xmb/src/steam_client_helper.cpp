#include "steam_library.h"

#include <isteamclient.h>
#include <isteamfriends.h>
#include <isteamuser.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace {

constexpr char OutputPrefix[] = "XMB_STEAM_FRIENDS:";
constexpr int MaximumFriends = 4096;
constexpr qsizetype MaximumPersonaBytes = 4096;
constexpr qsizetype MaximumPersonaCharacters = 256;

using CreateInterfaceFn = void* (*)(const char*, int*);

struct PipeSession final {
    ISteamClient* client = nullptr;
    HSteamPipe pipe = 0;
    HSteamUser user = 0;

    ~PipeSession() {
        if (client && user && pipe) client->ReleaseUser(pipe, user);
        if (client && pipe) client->BReleaseSteamPipe(pipe);
    }
};

QString boundedPersonaName(const char* value) {
    if (!value) return {};
    qsizetype length = 0;
    while (length < MaximumPersonaBytes && value[length] != '\0') ++length;
    return QString::fromUtf8(value, static_cast<int>(length)).left(MaximumPersonaCharacters);
}

QString jsonLine(QJsonObject result) {
    return QString::fromLatin1(OutputPrefix) +
           QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QJsonObject baseResult() {
    QJsonObject result;
    result.insert(QStringLiteral("steamId"), QString());
    result.insert(QStringLiteral("personaName"), QString());
    result.insert(QStringLiteral("friends"), QJsonArray());
    result.insert(QStringLiteral("error"), QString());
    result.insert(QStringLiteral("source"), QStringLiteral("steam-client"));
    return result;
}

int fail(const QString& message) {
    QJsonObject result = baseResult();
    result.insert(QStringLiteral("error"), message);
    const QByteArray bytes = jsonLine(result).toUtf8();
    fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    fputc('\n', stdout);
    fflush(stdout);
    return 2;
}

bool validSteamId(const QString& value, quint64* numeric) {
    if (value.isEmpty() || value.size() > 20) return false;
    for (const QChar ch : value) {
        if (!ch.isDigit() || ch.unicode() > 0x7f) return false;
    }
    bool ok = false;
    const quint64 parsed = value.toULongLong(&ok, 10);
    if (!ok || parsed == 0) return false;
    if (numeric) *numeric = parsed;
    return true;
}

QString steamError(DWORD code, const QString& detail) {
    return QStringLiteral("%1 (Windows error %2)").arg(detail).arg(code);
}

} // namespace

int main(int argc, char* argv[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    QString rootArgument;
    QString expectedArgument;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("--steam-root") && i + 1 < arguments.size()) {
            rootArgument = arguments.at(++i);
        } else if (arguments.at(i) == QStringLiteral("--expected-user") && i + 1 < arguments.size()) {
            expectedArgument = arguments.at(++i);
        } else {
            return fail(QStringLiteral("Invalid command line arguments."));
        }
    }
    if (rootArgument.trimmed().isEmpty())
        return fail(QStringLiteral("A Steam installation root is required."));

    quint64 expectedSteamId = 0;
    if (!expectedArgument.isEmpty() && !validSteamId(expectedArgument, &expectedSteamId))
        return fail(QStringLiteral("Expected Steam user must be a nonzero numeric 64-bit ID."));

    const QString root = QDir::cleanPath(QFileInfo(rootArgument).absoluteFilePath());
    const QString dllPath = QDir(root).filePath(QStringLiteral("steamclient64.dll"));
    if (!QFileInfo::exists(dllPath))
        return fail(QStringLiteral("steamclient64.dll was not found in the Steam installation root."));

    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
        return fail(steamError(GetLastError(), QStringLiteral("Could not configure safe DLL search paths.")));
    DLL_DIRECTORY_COOKIE directoryCookie = AddDllDirectory(reinterpret_cast<LPCWSTR>(root.utf16()));
    if (!directoryCookie)
        return fail(steamError(GetLastError(), QStringLiteral("Could not add the Steam installation directory to safe DLL search paths.")));
    const auto removeSteamDirectory = std::unique_ptr<void, decltype(&RemoveDllDirectory)>(
        directoryCookie, &RemoveDllDirectory);

    HMODULE module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(dllPath.utf16()), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
        return fail(steamError(GetLastError(), QStringLiteral("Could not load steamclient64.dll.")));
    // Deliberately keep the module loaded until process exit: Steam interfaces
    // may retain function pointers into the client DLL during pipe teardown.

    auto createInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
    if (!createInterface)
        return fail(QStringLiteral("steamclient64.dll does not export CreateInterface."));

    PipeSession session;
    session.client = static_cast<ISteamClient*>(createInterface(STEAMCLIENT_INTERFACE_VERSION, nullptr));
    if (!session.client)
        return fail(QStringLiteral("Steam client interface is unavailable."));
    session.pipe = session.client->CreateSteamPipe();
    if (!session.pipe)
        return fail(QStringLiteral("Steam could not create a client pipe."));
    session.user = session.client->ConnectToGlobalUser(session.pipe);
    if (!session.user)
        return fail(QStringLiteral("Steam could not connect to the signed-in global user."));

    ISteamUser* user = session.client->GetISteamUser(session.user, session.pipe, STEAMUSER_INTERFACE_VERSION);
    if (!user || !user->BLoggedOn())
        return fail(QStringLiteral("The Steam client is not logged on."));
    const CSteamID selfId = user->GetSteamID();
    const quint64 selfIdNumber = selfId.ConvertToUint64();
    if (selfIdNumber == 0)
        return fail(QStringLiteral("Steam returned an invalid signed-in user ID."));
    if (expectedSteamId != 0 && selfIdNumber != expectedSteamId)
        return fail(QStringLiteral("The signed-in Steam account does not match the expected user ID."));

    ISteamFriends* friendsInterface = session.client->GetISteamFriends(
        session.user, session.pipe, STEAMFRIENDS_INTERFACE_VERSION);
    if (!friendsInterface)
        return fail(QStringLiteral("Steam friends interface is unavailable."));

    const int friendCount = friendsInterface->GetFriendCount(k_EFriendFlagImmediate);
    if (friendCount < 0 || friendCount > MaximumFriends)
        return fail(QStringLiteral("Steam returned a friends list larger than the supported limit."));

    const xmb::SteamInstallation installation = xmb::scanSteam(root);
    QHash<QString, QString> titlesByAppId;
    titlesByAppId.reserve(installation.games.size());
    for (const xmb::SteamGame& game : installation.games)
        titlesByAppId.insert(game.appId, game.title);

    QJsonArray friendArray;
    for (int i = 0; i < friendCount; ++i) {
        const CSteamID friendId = friendsInterface->GetFriendByIndex(i, k_EFriendFlagImmediate);
        const quint64 friendIdNumber = friendId.ConvertToUint64();
        if (friendIdNumber == 0) continue;

        QJsonObject entry;
        entry.insert(QStringLiteral("steamId"), QString::number(friendIdNumber));
        QString name = boundedPersonaName(friendsInterface->GetFriendPersonaName(friendId));
        if (name.isEmpty()) name = QString::number(friendIdNumber);
        entry.insert(QStringLiteral("name"), name);
        const int state = static_cast<int>(friendsInterface->GetFriendPersonaState(friendId));
        entry.insert(QStringLiteral("personaState"), std::clamp(state, 0, 6));

        FriendGameInfo_t gameInfo{};
        QString gameTitle;
        QString appIdString;
        if (friendsInterface->GetFriendGamePlayed(friendId, &gameInfo)) {
            const quint32 appId = gameInfo.m_gameID.AppID();
            if (appId != 0) {
                appIdString = QString::number(appId);
                gameTitle = titlesByAppId.value(appIdString);
            }
        }
        if (gameTitle.isEmpty() && !appIdString.isEmpty()) gameTitle = QStringLiteral("Steam game");
        entry.insert(QStringLiteral("game"), gameTitle);
        entry.insert(QStringLiteral("gameAppId"), appIdString);
        friendArray.append(entry);
    }

    QJsonObject result = baseResult();
    result.insert(QStringLiteral("steamId"), QString::number(selfIdNumber));
    result.insert(QStringLiteral("personaName"), boundedPersonaName(friendsInterface->GetPersonaName()));
    result.insert(QStringLiteral("friends"), friendArray);
    const QByteArray bytes = jsonLine(result).toUtf8();
    fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    fputc('\n', stdout);
    fflush(stdout);
    return 0;
}
