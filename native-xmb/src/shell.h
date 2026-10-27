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
#include "subtitles.h"
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
enum class Action { Unavailable, SettingsPage, SettingChoices, SetSetting, EditName, CreateUser, SelectUser, Wallpaper, Theme, Information, Library, ChooseLibrary, File, Folder, Launch, LaunchSteam, InstallSteam, SteamFolder, SteamStoreBrowse, SteamStoreSection, LaunchApp, OpenUrl, RefreshContent, RefreshFriends, ImportApps, JellyfinApp, JellyfinFolder, JellyfinPlay, JellyfinMore, JellyfinSignOut, SteamDownloads };
struct Entry {
    QString id,title,detail,container,texture;
    Action action=Action::Unavailable;
    QString value;
    QString setting;
    QVariant option;
    QString themeIcon; // the PS3 theme (.p3t icontable) id that replaces this icon, e.g. "icon_game_setting"
    QImage artwork; // provider images decoded off the render thread
    // Where a generated row's title and icon came from: "xmbml" (the view's title_rsc/icon_rsc), "rco"
    // (a label the firmware builds in a plugin, resolved through its own RCO strings), "builtin" (the
    // last-resort literal this build shipped) or "fallback" (the whole column kept its built-in rows).
    // Empty on hand-written rows, so the entry dump stays quiet about the rest of the XMB.
    QString source;
    bool gameTile=false,friendRow=false;
    int presenceState=0;
};
struct Category { QString id,title,container,texture; QVector<Entry> entries; QString themeIcon; };
// Item-level Triangle options menu: the PS3 shows a right-hand panel, not a level. Each row is a command
// this build really performs; the list is built from the selected entry so other surfaces inherit it.
// The same panel carries the in-player rows (subtitles, audio tracks, repeat, shuffle), which is where the
// firmware's video control panel keeps its subtitle and audio-change buttons.
enum class OptionCommand { Activate, Information, SortByNameAscending, SortByNameDescending, SortMenu, Close,
    SubtitlesOff, SubtitleTrack, AudioTrack, RepeatMode, ToggleShuffle };
struct MenuOption {
    QString title;
    OptionCommand command=OptionCommand::Close;
    int index=-1;          // the row's payload: a subtitle track index or an audio track index
    bool enabled=true;     // false: listed but not selectable, and `note` says why (no pretending)
    bool active=false;     // the current choice, marked in the player panel
    QString note;          // one line under the list while this row has focus (a reason, a file name)
};
// One audio track QMediaPlayer reports for the loaded media. `switchable` turns false the moment this
// build asked for it and the player's own activeAudioTrack() did not follow, and then `reason` says so.
struct AudioTrackInfo { QString label; bool switchable=true; QString reason; };
// The console's notification pop-up. The firmware builds it from system_plugin's page_notification page:
// a Plane carrying tex_notification (the 64x64 plaque), a notification_icon_a/b Plane carrying a
// tex_notification_* 100x100 glyph, and Text objects to the right of the icon (notification_message_a in
// the one-line variant, notification_label0_b/1_b/2_b in the three-line one). The page is composed by
// basic_plugins.prx FUN_0000773c, which grows the plate by 1 unit per text row past three. This shell keeps
// the queue and the lifetime; View draws the composite (audit/NOTIFICATIONS.md).
struct Notification {
    QString title;   // notification_message_a / notification_label0_b
    QString detail;  // notification_label1_b; empty for the one-line variant
    QString icon;    // a system_plugin texture name: tex_notification, tex_notification_<kind>
    qint64 expiresAt=0; // Shell's notification clock, milliseconds
};
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
    // PS button (DualSense/DualShock over HID, keyboard substitute): a tap shows the controller indicator
    // and returns focus to the XMB; a hold opens the system menu the console shows (captured in RPCS3:
    // Turn Off the System / Turn Off the Controller, plus Quit Game while a title runs).
    void psTapped();
    void psHeld();
    bool psMenuOpen() const { return psMenu_; }
    int psMenuSelection() const { return psSelection_; }
    QStringList psMenuItems() const;
    void psMenuMove(int delta);
    void psMenuActivate();
    void psMenuClose();
    bool psMenuConfirmingOff() const { return psConfirmOff_; }
    // Triangle options panel for the selected entry (a right-hand panel; the XMB behind it keeps its layout).
    bool optionsOpen() const { return optionsOpen_; }
    int optionsSelection() const { return optionsSelection_; }
    const QVector<MenuOption>& options() const { return options_; }
    QString optionsTitle() const { return optionsTitle_; }
    void toggleOptions();          // Triangle: open, or dismiss the whole menu
    void optionsMove(int delta);   // Up/Down inside the panel
    void optionsActivate();        // Cross
    void optionsClose();           // Circle: step back one level, then close
    bool optionsArePlayer() const { return optionsOpen_ && playerPanel_; } // the in-player panel is up
    // ---- Sidecar subtitles and audio tracks (audit/SUBTITLES.md) ----
    // The subtitle files discovered beside the video being played, the chosen one (index -1 = Off) and
    // the cue under the player's own position. The choice is remembered per file for this session only.
    const QVector<subtitles::Track>& subtitleTracks() const { return subtitleTracks_; }
    int subtitleTrackIndex() const { return subtitleTrackIndex_; }
    QString subtitleFile() const;    // the chosen sidecar path, empty when subtitles are Off
    QString subtitleLabel() const;   // its panel label, empty when Off
    QString subtitleCueText();       // the cue covering mediaPosition(), empty when none is showing
    int subtitleCueNumber();         // 1-based cue number at mediaPosition(), 0 when none
    const QVector<AudioTrackInfo>& audioTracks() const { return audioTracks_; }
    int activeAudioTrack() const { return activeAudioTrack_; } // 0-based, -1 = the backend names none
    void chooseSubtitleTrack(int index); // -1 = Off; anything else indexes subtitleTracks()
    void chooseAudioTrack(int index);    // set the track, then verify the backend's own read-back
    // The notification queue: one pop-up at a time, each shown for a fixed lifetime, the rest waiting in
    // order behind it. Bounded, so a failing producer cannot grow it without limit.
    static constexpr qint64 notificationLifetimeMs=4000;
    static constexpr int notificationQueueLimit=4;
    void notify(const QString& title,const QString& detail={},const QString& icon={});
    const Notification* notification() const { return notifications_.isEmpty()?nullptr:&notifications_.first(); }
    int notificationCount() const { return notifications_.size(); }
    qint64 notificationRemainingMs() const; // of the pop-up on screen, 0 when there is none
signals:
    void changed();
    void requestLibrary();
    void mediaFrame(const QImage& frame);
    void requestWallpaper();
    void requestTheme();
    void requestSystemName(const QString& value);
    void requestQuit();
    void psTapIndicator();
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
    // Download Management (the firmware's seg_dlctrl row): the page's rows are Steam's own download
    // queue through scanSteamDownloads(), kept in the scanner's order and rebuilt in place by F5
    // (audit/STEAM-DOWNLOADS.md section 10).
    QVector<Entry> steamDownloadRows(QString& error) const;
    void openSteamDownloads();
    void reloadSteamDownloads();
    quint64 scanRequest_=0,photoRequest_=0;
    bool loading_=false;
    QString status_;
    QTimer slideshow_,idle_;
    QElapsedTimer activity_;
    qint64 idleTimeOverride_=-1;
    bool sessionHold_=false;         // the host display-awake request currently in effect
    QImage photo_;
    int photoRotation_=0;
    QVector<Category> categories_;
    QVector<int> selections_;
    struct Page { QString title; QVector<Entry> entries; int selection=0; QString directory; bool jellyfin=false,jellyfinViews=false; QString jellyfinParent; int jellyfinStart=0; bool downloads=false; };
    QVector<Page> pages_;
    bool psMenu_=false;              // the PS-button system menu is showing
    bool psConfirmOff_=false;        // its "Turn Off the System" confirmation step
    int psSelection_=0;
    // Item options panel state: the list itself, one parent level (Sort By), and the selection.
    bool optionsOpen_=false,optionsNested_=false;
    int optionsSelection_=0;
    QVector<MenuOption> options_,optionsParent_;
    QString optionsTitle_;
    void buildOptions();
    // The in-player variant of the same panel: Subtitles Off / one row per sidecar file, the audio
    // tracks the player reports, then Repeat and Shuffle (which used to hold the face buttons).
    bool playerPanel_=false;
    void buildPlayerOptions();
    void refreshPlayerOptions();
    void dismissOptions();
    void resetOptions();
    QString activateLabel(const Entry& entry) const;
    QString entryKind(const Entry& entry) const;
    void showEntryInformation(const Entry& entry);
    void sortPageByName(bool ascending);
    // Subtitle and audio-track state. Discovery and parsing live in subtitles.cpp; this class owns the
    // choice, the cue lookup against the player's position and the session memory of that choice.
    bool subtitleLog_=false;
    QVector<subtitles::Track> subtitleTracks_;
    int subtitleTrackIndex_=-1;
    int subtitleCueNumber_=0;
    QString subtitleMediaPath_;          // the local file the tracks belong to
    QHash<QString,QString> subtitleChoice_; // media path -> sidecar path ("" = Off), this session only
    QVector<AudioTrackInfo> audioTracks_;
    QHash<int,QString> audioRejected_;   // track index -> why the backend would not switch to it
    int activeAudioTrack_=-1;
    void prepareSubtitles(const QString& path); // discover beside a file that is starting to play
    void applySubtitleTrack(int index);
    void refreshAudioTracks();
    // Notification queue state. The front entry is the pop-up on screen; its lifetime starts when it is
    // promoted to the front, so a burst of events each gets its full 4 s in turn.
    QVector<Notification> notifications_;
    QTimer notificationTimer_;
    QElapsedTimer notificationClock_;
    bool notificationLog_=false;
    void promoteNotification();
    void expireNotifications();
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
    // Set by refreshDirectory() so the scan's result reports itself as a rescan of an already-open page
    // (the deterministic media event behind a "library updated" notification).
    bool directoryRefresh_=false;
    void openSettings(const QString& page);
    void openChoices(const QString& key,const QString& title);
    void applyPreferences();
    void touch();
    void stepPhoto(int delta);
    void showPhoto();
    void rebuildUsers();
    void addInstalledApps();
    // Builds the Network column's rows from the firmware's own category_network.xml (audit/XMBML-NETWORK.md),
    // keeping the built-in rows when the definition cannot be read.
    void resolveNetworkColumn();
    // RPCS3 next to dev_hdd0 (the emulator whose dev_flash/dev_hdd0 we read), or empty.
    QString emulatorPath() const;
    void launch(const Entry& entry);
    int& currentSelection() { return pages_.isEmpty()?selections_[category_]:pages_.last().selection; }
};
}
