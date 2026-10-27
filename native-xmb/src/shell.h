#pragma once
#include <QObject>
#include "preferences.h"
#include "media_library.h"
#include "playback_queue.h"
#include "interaction_audio.h"
#include "profiles.h"
#include "steam_friends.h"
#include "content_registry.h"
#include "jellyfin_client.h"
#include "jellyfin_stream.h"
#include "steam_owned.h"
#include "steam_collections.h"
#include "steam_family.h"
#include "steam_store.h"
#include "steam_library.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QVector>
#include <QImage>
#include <memory>
#include <QPointer>
#include <QProcess>
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;

namespace xmb {
class Assets;
class SteamArtwork;
enum class Action { Unavailable, SettingsPage, SettingChoices, SetSetting, EditName, CreateUser, SelectUser, Wallpaper, Theme, Information, Library, ChooseLibrary, File, Folder, Launch, LaunchSteam, InstallSteam, SteamFolder, SteamStoreBrowse, SteamStoreSection, LaunchApp, OpenUrl, RefreshContent, RefreshFriends, ImportApps, JellyfinApp, JellyfinFolder, JellyfinPlay, JellyfinMore, JellyfinSignOut };
struct Entry {
    QString id,title,detail,container,texture;
    Action action=Action::Unavailable;
    QString value;
    QString setting;
    QVariant option;
    QString themeIcon; // the PS3 theme (.p3t icontable) id that replaces this icon, e.g. "icon_game_setting"
    QImage artwork; // provider images decoded off the render thread
    bool gameTile=false,friendRow=false;
    int presenceState=0;
};
struct Category { QString id,title,container,texture; QVector<Entry> entries; QString themeIcon; };
class Shell final:public QObject {
    Q_OBJECT
public:
    Shell(QString dataDirectory,Assets& assets,QObject* parent=nullptr);
    ~Shell() override;
    const QVector<Category>& categories() const { return categories_; }
    const QVector<Entry>& entries() const;
    int category() const { return category_; }
    int selection() const { return pages_.isEmpty()?selections_[category_]:pages_.last().selection; }
    int depth() const { return int(pages_.size()); }
    const QVector<Entry>& entriesAt(int depth) const;
    int selectionAt(int depth) const;
    QString pageTitle() const { return pages_.isEmpty()?QString{}:pages_.last().title; }
    QString message() const { return message_; }
    void setRenderer(const QString& renderer) { renderer_=renderer; }
    int theme() const { return preferences_.value("appearance/colour").toInt(); }
    QString fontFamily() const;
    QString valueText(const Entry& entry) const;
    // Small line under a row (reference: the signed-in user's name under user-scoped items like Trophy Collection).
    QString subtitleText(const Entry& entry) const;
    QString clockText() const;
    QDateTime clockDateTime() const;
    // Local capture override only; never changes or persists the host clock.
    void setClockOverride(const QDateTime& when) { clockOverride_=when;emit changed(); }
    int backgroundMode() const { return preferences_.value("appearance/background").toInt(); }
    QString wallpaper() const { return preferences_.value("appearance/wallpaper").toString(); }
    // The PS3 theme in use (Theme Settings > Theme), decoded in memory from its .p3t: empty path = the system's own.
    QString themePath() const { return theme_.path; }
    const QImage& themeBackground() const { return theme_.background; }
    QImage themeIcon(const QString& id) const { return id.isEmpty()?QImage{}:theme_.icons.value(id); }
    bool fullscreen() const { return preferences_.value("display/fullscreen").toBool(); }
    bool photoFill() const { return preferences_.value("photo/fit").toInt()==1; }
    bool photoOpen() const;
    bool slideshowActive() const { return slideshow_.isActive(); }
    bool busy() const { return loading_; }
    QString status() const { return status_; }
    qint64 mediaPosition() const;
    qint64 mediaDuration() const;
    bool mediaPaused() const;
    bool videoOpen() const;
    void seekMedia(qint64 delta);
    QString appRegistryPath() const;
    void noteActivity() { touch(); }
    qint64 idleMilliseconds() const { return idleTimeOverride_>=0?idleTimeOverride_:activity_.elapsed(); }
    void setIdleTimeOverride(qint64 milliseconds) { idleTimeOverride_=milliseconds;emit changed(); }
    int screensaverMinutes() const { return preferences_.value("display/screensaverMinutes").toInt(); }
    bool idleDisplayAllowed() const;
    bool mediaOpen() const { return mediaOpen_; }
    QString mediaTitle() const { return mediaTitle_; }
    QString mediaProvider() const { return jellyfinQueue_?QString("Jellyfin"):QString("Local"); }
    QString mediaPlayMethod() const { return jellyfinQueue_?jellyfinPlayback_.playMethod:QString{}; }
    QString mediaQueueSummary() const;
    int mediaRepeatMode() const;
    void skipMedia(int direction);
    void cycleMediaRepeat();
    void toggleMediaShuffle();
    void horizontal(int delta);
    void vertical(int delta);
    void activate();
    void back();
    void chooseLibrary(const QString& folder);
    void togglePlayback();
    void chooseWallpaper(const QString& file);
    void chooseTheme(const QString& file);
    void setSystemName(const QString& name);
    void setFullscreen(bool enabled);
    void refreshDirectory();
    void refreshContent();
    void importApps(const QString& file);
    void authenticateJellyfin(QUrl server,QString username,QString password);
    void cancelJellyfinSignIn();
    void createUser(const QString& name);
signals:
    void changed();
    void requestLibrary();
    void mediaFrame(const QImage& frame);
    void requestWallpaper();
    void requestTheme();
    void requestSystemName(const QString& value);
    void requestQuit();
    void requestNewUser();
    void requestImportApps();
    void requestJellyfinSignIn(QUrl server);
    void jellyfinSignInFinished(bool success,QString error);
private:
    Assets& assets_;
    Preferences preferences_;
    MediaLibrary library_;
    InteractionAudio interactionAudio_;
    Profiles profiles_;
    QString dataDirectory_,steamRoot_,steamId_;
    QVector<ContentApp> contentApps_;
    JellyfinClient jellyfin_;
    JellyfinStream jellyfinStream_;
    JellyfinPlayback jellyfinPlayback_;
    QTimer jellyfinProgress_;
    quint64 jellyfinRequest_=0;
    int jellyfinRequestDepth_=0,jellyfinRootDepth_=0;
    bool jellyfinSigningIn_=false,jellyfinQueue_=false,jellyfinStarted_=false;
    QString jellyfinServiceTitle_,jellyfinRequestedTitle_;
    QHash<QString,bool> jellyfinAudio_;
    void initJellyfin();
    void openJellyfin(const Entry& entry);
    void loadJellyfinPage(const QString& parent,const QString& title,int start=0,bool views=false,bool replace=false);
    void openJellyfinMedia(const Entry& entry);
    void startJellyfinMedia();
    void finishJellyfinPlayback();
    void signOutJellyfin();
    SteamFriends steamFriends_;
    SteamOwned steamOwned_;
    OwnedGameList ownedGames_;      // purchased titles from the Steam Web API (may be empty without a key)
    SteamCollectionList collections_; // the account's own Steam library collections
    SteamFamily family_;              // the Steam Families group this account belongs to
    QVector<FamilyGame> familyGames_; // optional family-shared titles from <data>/family-shared.json
    SteamInstallation steamInstalled_; // last local Steam scan, for the library folders
    SteamStore steamStore_;          // public storefront browser (browse + install handoff)
    QHash<int,QString> mediaTags_;   // per queue index, track tags read from the player
    QString mediaAlbum_;             // album of the loaded track, when tagged
    QString resumePath_;             // file whose playback may resume
    qint64 resumePending_=0;         // milliseconds to seek once the media reports Loaded
    QPointer<QProcess> game_;        // the launched title, kept so a return is observable
    QString gameTitle_;              // its title, for the return message
    QString steamGameId_;            // a Steam title handed to steam.exe, watched through the registry
    QTimer steamWatch_;              // polls Steam's Running flags while steamGameId_ is set
    QElapsedTimer steamWatchAge_;    // since the handoff (or last update tick), for the start timeout
    bool steamGameSeen_=false;       // Steam has reported the title running at least once
    int steamGameClear_=0;           // consecutive polls without the flag after it was seen
    bool ownedInFlight_=false;
    QString ownedSignature_;        // app ids + installed flags + collection ids, to skip unchanged rebuilds
    std::unique_ptr<SteamArtwork> steamArtwork_;
    QTimer steamRefresh_;
    QElapsedTimer friendsAge_;
    SteamFriendList steamFriendList_;
    QString steamFriendsError_;
    bool contentScanning_=false,contentLoaded_=false,contentRefreshPending_=false,friendsRefreshing_=false,showFriendsErrors_=false;
    void rebuildFriends();
    void refreshSteamFriends(bool showErrors=false,bool force=false);
    void refreshOwnedGames();
    bool stageOwnedForRefresh() const;
    QString collectionLabel(const QString& appId) const;
    bool startTracked(const QString& program,const QStringList& arguments,const QString& workingDirectory,const QString& title);
    bool gameRunning() const;
    bool startSteamTracked(const QString& appId,const QString& title);
    void pollSteamGame();
    void restoreFocus();
    int steamTitleCount() const;
    QVector<Entry> steamFolderEntries(const QString& mode) const;
    void openSteamFolder(const QString& mode);
    quint64 scanRequest_=0,photoRequest_=0;
    bool loading_=false;
    QString status_;
    QTimer slideshow_,idle_;
    QElapsedTimer activity_;
    qint64 idleTimeOverride_=-1;
    QImage photo_;
    int photoRotation_=0;
    QVector<Category> categories_;
    QVector<int> selections_;
    struct Page { QString title; QVector<Entry> entries; int selection=0; QString directory; bool jellyfin=false,jellyfinViews=false; QString jellyfinParent; int jellyfinStart=0; };
    QVector<Page> pages_;
    QString message_,mediaTitle_,renderer_;
    QDateTime clockOverride_;
    // Only the decoded pieces are kept (the .p3t itself is read again when the theme changes); nothing is written to disk.
    struct Theme { QString path,name; QImage background; QHash<QString,QImage> icons; };
    Theme theme_;
    void loadTheme();
    bool mediaOpen_=false;
    PlaybackQueue mediaQueue_;
    quint64 mediaRequest_=0;
    int mediaFailures_=0;
    void startQueuedMedia();
    qint64 resumePositionFor(const QString& path) const;
    void storeResumePosition(const QString& path,qint64 positionMs);
    void clearResumePosition(const QString& path);
    void advanceMedia(bool failed,const QString& error={});
    int category_=6;
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> audio_;
    std::unique_ptr<QVideoSink> video_;
    void openDirectory(const QString& path);
    void openFile(const Entry& entry);
    void openSettings(const QString& page);
    void openChoices(const QString& key,const QString& title);
    void applyPreferences();
    void touch();
    void stepPhoto(int delta);
    void showPhoto();
    void rebuildUsers();
    void addInstalledApps();
    // RPCS3 next to dev_hdd0 (the emulator whose dev_flash/dev_hdd0 we read), or empty.
    QString emulatorPath() const;
    void launch(const Entry& entry);
    int& currentSelection() { return pages_.isEmpty()?selections_[category_]:pages_.last().selection; }
};
}
