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
#include <functional>
#include <memory>
#include <QPointer>
#include <QProcess>
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;

namespace xmb {
class Assets;
class SteamArtwork;
enum class Action { Unavailable, SettingsPage, SettingChoices, SetSetting, EditName, CreateUser, SelectUser, Wallpaper, Theme, Information, Library, ChooseLibrary, File, Folder, Launch, LaunchSteam, InstallSteam, SteamFolder, SteamStoreBrowse, SteamStoreSection, LaunchApp, OpenUrl, RefreshContent, RefreshFriends, ImportApps, JellyfinApp, JellyfinFolder, JellyfinPlay, JellyfinMore, JellyfinSignOut, SteamDownloads,
    // A free-text search of the signed-in server's own library: the row asks the view for the XMB
    // keyboard (through requestJellyfinFieldEdit with the field key "search") and pushes a result
    // page whose rows carry the same Play/Resume semantics as a folder.
    JellyfinSearch,
    // The in-XMB Jellyfin sign-in page (audit/JELLYFIN-NATIVE.md, "Sign-in inside the XMB"). A field row opens
    // the on-screen keyboard for that field (Entry::value names it: server, username, password), Submit sends
    // the request and Cancel leaves the page and aborts anything in flight. They are ordinary rows on an
    // ordinary page, so the pad reaches them through the same horizontal/vertical/activate/back path as
    // every other row and no modal dialog can turn pollController off.
    // JellyfinSignInRemember is the explicit "stay signed in" opt-in: Cross toggles it, and only an
    // accepted sign-in with it on writes the DPAPI-protected session record.
    JellyfinSignInField, JellyfinSignInSubmit, JellyfinSignInCancel, JellyfinSignInRemember };
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
    // A row this build cannot act on but still lists, so the reason stays reachable and the state stays
    // visible: drawn dimmed, and only the Continue Watching page sets it today (a remembered title whose
    // file is gone). Everything else leaves it false, so no other row renders differently.
    bool disabled=false;
    // The value drawn on the right of a focused row, above every other source the view consults. The sign-in
    // page's three fields use it (server URL, user name, and the password as dots); every other row leaves it
    // empty, so the view falls through to Shell::valueText() exactly as before and no existing frame changes.
    QString fieldValue;
    // Remote media progress supplied by the authenticated provider, never a local file resume key.
    QString mediaKind;
    qint64 resumeMs=0,durationMs=0;
    bool played=false;
};
struct Category { QString id,title,container,texture; QVector<Entry> entries; QString themeIcon; };
// Item-level Triangle options menu: the firmware builds it per item from explore_plugin_full's English text
// table, with Information always the last row and the other rows appearing only under their own conditions
// (audit/ITEM-MENU-INFO-FIRMWARE.md A3-A4). There is no Close row: Circle/Escape close the list.
// The same panel carries the in-player rows (subtitles, audio tracks, repeat, shuffle), which is where the
// firmware's video control panel keeps its subtitle and audio-change buttons.
enum class OptionCommand { Activate, Information,
    // The firmware's date-pair sort rows (msg_*_date_f/_r); the sort itself is this build's, over the
    // timestamps the listed files and folders actually carry.
    SortCreatedAscending, SortCreatedDescending, SortUpdatedAscending, SortUpdatedDescending, SortMenu,
    // The firmware's per-item file commands. Listed (not selectable) only for local content, because this
    // build has no copy or delete backend yet; the row's note says so.
    Copy, Delete,
    SubtitlesOff, SubtitleTrack, AudioTrack, RepeatMode, ToggleShuffle,
    // The Continue Watching page's own command: forget the remembered position for this row. Only the
    // page that lists resume points offers it (buildOptions gates on the row id). This command has no
    // firmware row, so its label is the one native wording the item menu keeps.
    RemoveResume,
    // msg_play_begin: restart from the top when a resume record exists (a Jellyfin item or a local file).
    PlayFromBeginning };
// A label read from the RCO text table: the key plus the English string it should resolve to. The fallback
// is used only when the local flash image has no table; whenever the RCO is present, its value is the label.
struct OptionLabel { const char* key=nullptr; QString fallback; };
struct MenuOption {
    QString title;
    OptionCommand command=OptionCommand::Activate;
    int index=-1;          // the row's payload: a subtitle track index or an audio track index
    bool enabled=true;     // false: listed but not selectable, and `note` says why (no pretending)
    bool active=false;     // the current choice, marked in the player panel
    QString note;          // one line under the list while this row has focus (a reason, a file name)
    QString labelKey;      // the explore_plugin_full key the title was read from; empty when none exists
    QString condition;     // why the row is in this particular list (the panel log and the options dump)
    // True on the first row of a visual group that starts below the firmware's gap. The builder sets it on
    // the first item action when the list-level/utility rows above it exist; it is the only group data the
    // view gets, so the view owns the measured y anchors and no per-context offset lives in the shell
    // (audit/ITEM-MENU-INFO-FIRMWARE.md G2: Sort By/Group Content ink 301-362, Start/Delete/Information
    // 539-638, with the gap between).
    bool dividerBefore=false;
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
// The surface a message is drawn on. The firmware assembles each of the five from a different set of
// pieces (audit/DIALOGS-FIRMWARE.md section 2), so the kind is a decision the shell makes where it sets
// the message, not something the view infers from the string:
//   Inline     D3 - one line in the list's own label column. No plate, rules, hint or frame treatment
//                   (measured, frame 14.4 s: "There are no content items." at x 630, centre y 504).
//   Page       D4 - icon + left title above a flat rule at y 197, centred body at y 509, flat rule at
//                   y 877, one Back hint at x 1155 (measured, frame 20.6 s). No dim.
//   Box        D1 - the run-time message box: the RCO plate (tex_msgdialog_bg over tex_msgdialog_bg2),
//                   exactly two tex_msgdialog_bar bars at the box's own width, and a two-button hint row
//                   (Cross msg_ok / Circle msg_cancel). Spec-only: no local frame shows a modal.
//   ErrorCode  D2 - the D4 frame around a scrollable Suggested Actions body with Enter/Back hints.
//                   Spec-only: no local frame shows an error-code page.
//   Info       D5 - the D4 frame with Key: value rows built from firmware strings.
enum class MessageKind { None, Inline, Page, Box, ErrorCode, Info };
// The Box's answers, in the shell's own model order: No is 0 because it is the answer the captured frame
// focuses - the console's default and the non-destructive one - and Yes is 1. The box DRAWS them the other
// way round: Yes is the left label and No the right (audit/DIALOGS-FIRMWARE.md 10.2 / section 11), so this
// index never encodes screen space. The one place the two orders meet is Shell::boxChoiceSlot().
enum class BoxChoice { No = 0, Yes = 1 };
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
    // End-of-list overscroll (GRAPHICS-INVENTORY #13): DOWN on the last row does not move the focus; the whole
    // column sinks 40 px per press and stays sunk. The count lives here because this object knows the move was
    // clamped; View::setColumnSink() projects it.
    int columnSink() const { return columnSink_; }
    QString pageTitle() const { return pages_.isEmpty()?QString{}:pages_.last().title; }
    QString message() const { return message_; }
    // Which surface the message is drawn on (see MessageKind). message() carries the text: for Inline the
    // line itself, for the other kinds "Title\n\nbody" where a single first line is the header.
    MessageKind messageKind() const { return messageKind_; }
    // The Suggested Actions bullets of an ErrorCode page (the firmware's msg_errorcode_solution_* family;
    // this build's actions are its own because no local firmware code produces a PS3 error code here).
    const QStringList& messageActions() const { return messageActions_; }
    // The Box's answer row. 0 = No (the default showConfirm starts on), 1 = Yes, and -1 when the message is not
    // a Box with answers: an information-only Box raised with showMessage keeps the single-action behaviour it
    // has always had, so no call site suddenly needs a choice. Left/Right move the pair (the same keys and pad
    // bits that already reach Shell::horizontal), Cross confirms the focused answer, Circle cancels.
    int boxChoice() const;
    // The same focus in the order the box draws it: 0 = Yes (the left answer), 1 = No (the right one), -1 = no
    // answer row. dialogs.cpp compares its {Yes, No} pair against this, so the halo marks the live selection
    // instead of the console's default. The frame puts Yes at x 1183 and No at x 1341 (audit 10.2).
    int boxChoiceSlot() const;
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
    // The photo viewer's rotate pair. The console puts Rotate Left/Right on the viewer's own control
    // panel (audit/MEDIA-PANELS-FIRMWARE.md 6.2, the same panel's Up/Down/Left/Right are the cursor
    // buttons), and this build does not draw that panel; the native keyboard keeps the function on Q/E
    // and the script's ROTATELEFT/ROTATERIGHT tokens. Up/Down no longer rotate (BEHAVIOUR-GAP-AUDIT A32).
    void rotatePhoto(int delta);
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
    // ---- In-XMB Jellyfin sign-in (audit/JELLYFIN-NATIVE.md) ----
    // A real page in the existing page/column model, pushed where the shell used to open a modal Qt dialog:
    // rows Server URL, Username, Password, Sign In, Cancel. Cross on a field asks the view for the on-screen
    // keyboard (masked for the password), Submit sends the request, Cancel leaves the page and aborts an
    // in-flight one. The values live in this process's memory only - nothing is written to disk and no log
    // line carries them.
    void openJellyfinSignInPage(QUrl server);
    bool jellyfinSignInOpen() const;
    void setJellyfinSignInField(const QString& field,const QString& value);
    void submitJellyfinSignIn();
    void cancelJellyfinSignInPage();
    QString jellyfinSignInServer() const { return signInServer_; }
    QString jellyfinSignInUsername() const { return signInUser_; }
    QString jellyfinSignInPassword() const { return signInPassword_; } // masked wherever it is displayed
    // "idle", "signingIn" or "failed" while the page is up; empty when it is not. The state the grab sidecar
    // reports, so a scripted run can check the page without reading pixels.
    QString jellyfinSignInState() const;
    QString jellyfinSignInError() const { return signInError_; }
    // What the page under the cursor is, for the capture sidecar: "jellyfinSignIn", "jellyfinViews",
    // "jellyfinFolder", "downloads", "continueWatching", "folder", "page" or "root".
    QString pageKind() const;
    // The open Jellyfin page's backdrop, fetched through the authenticated image route and kept for
    // the view's hero surface. Nothing draws it yet: the view has no page-level artwork slot, so the
    // hunk that would use it is reported in audit/JELLYFIN-NATIVE.md instead of being written here.
    const QImage& pageBackdrop() const { return jellyfinBackdrop_; }
    // True while a DPAPI session record exists for this installation (the "stay signed in" opt-in).
    bool jellyfinRemembered() const;
    QString focusedRowId() const;
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
    // Let a view-owned surface play a system cue through the shell's one audio player (the music
    // control panel opens with the firmware's message cue; audit MEDIA-PANELS-FIRMWARE 7.2/11.7).
    void playCue(InteractionAudio::Cue cue) { interactionAudio_.play(cue); }
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
    // Cross on a sign-in field: the view opens the on-screen keyboard on `initial` and returns what the user
    // accepted through Shell::setJellyfinSignInField. `masked` makes the keyboard compose dots.
    void requestJellyfinFieldEdit(QString field,QString title,QString initial,bool masked);
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
    QUrl jellyfinServiceServer_;      // the registration's URL, for a sign-in page reached after a refused restore
    QHash<QString,bool> jellyfinAudio_;
    QHash<QString,qint64> jellyfinResume_;
    qint64 jellyfinResumePending_=0;
    bool jellyfinApplyingResume_=false;
    bool jellyfinRestoring_=false;    // a remembered session's first page is loading
    bool signInRemember_=false;       // the sign-in page's opt-in row
    QString jellyfinSearchTerm_;      // the last accepted search term, prefilled into the keyboard
    QImage jellyfinBackdrop_;         // the open page's hero image (no view surface yet)
    QString jellyfinBackdropId_;
    // In-XMB sign-in page state (audit/JELLYFIN-NATIVE.md). The server comes from the registration; the user
    // name and password are typed on the XMB's own keyboard and stay in memory for this process only, and the
    // password is cleared when the page closes. signInError_ is the client's own message, shown on the page.
    QString signInServer_,signInUser_,signInPassword_,signInError_;
    void refreshJellyfinSignInPage();  // rebuild the page's rows in place, keeping the focused row
    void closeJellyfinSignInPage();    // pop it and forget the password
    void initJellyfin();
    void openJellyfin(const Entry& entry);
    void loadJellyfinPage(const QString& parent,const QString& title,int start=0,bool views=false,bool replace=false);
    void openJellyfinSearch(const QString& term);
    void loadJellyfinSearchPage(const QString& term,const QString& title,int start=0,bool replace=false);
    void openJellyfinMedia(const Entry& entry,bool fromBeginning=false);
    void startJellyfinMedia();
    void applyJellyfinResume();
    void reportJellyfinPlaybackState();
    void finishJellyfinPlayback(bool completed=false);
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
    int columnSink_=0;
    struct Page { QString title; QVector<Entry> entries; int selection=0; QString directory; bool jellyfin=false,jellyfinViews=false,jellyfinSearch=false; QString jellyfinParent; int jellyfinStart=0; bool downloads=false; bool continuePage=false; bool signIn=false; };
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
    // Item-menu rows and labels. Every title comes from explore_plugin_full's English table (A3); the row's
    // labelKey and condition travel with it so the panel log and the options dump can show what was built
    // and why (the same evidence path XMB_DUMP_ENTRIES already writes for the catalogue and folder pages).
    OptionLabel activateLabel(const Entry& entry) const;
    QString optionLabel(const OptionLabel& label);
    void addOption(const OptionLabel& label,OptionCommand command,const char* condition,bool enabled=true,
                   const QString& note={},int index=-1);
    qint64 optionResumePoint(const Entry& entry) const;   // 0 = no resume record for this row
    bool optionIsLocalContent(const Entry& entry) const;
    bool optionPageSortable() const;
    void logOptions(const Entry& entry);
    // The in-player variant of the same panel: Subtitles Off / one row per sidecar file, the audio
    // tracks the player reports, then Repeat and Shuffle (which used to hold the face buttons).
    bool playerPanel_=false;
    bool playFromBeginning_=false;   // one-shot: start the next local file at 0 instead of its stored position
    void buildPlayerOptions();
    void refreshPlayerOptions();
    void dismissOptions();
    void resetOptions();
    QString entryKind(const Entry& entry) const;
    void showEntryInformation(const Entry& entry);
    void sortPageByDate(bool ascending,bool created);
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
    // The one place a message is raised or dismissed. The kind decides the surface View draws
    // (MessageKind); a Box also carries the action Cross accepts, so the confirm cue and the action are
    // the box's own (TRACED 0x00341870: snd_decide on the box's confirm button).
    void showMessage(const QString& text,MessageKind kind);
    void showMessage(const QString& title,const QString& body,MessageKind kind);
    void showConfirm(const QString& title,const QString& body,std::function<void()> accepted);
    void showErrorPage(const QString& title,const QString& cause,const QStringList& actions);
    void clearMessage();
    QString message_;
    MessageKind messageKind_=MessageKind::None;
    QStringList messageActions_;
    std::function<void()> messageAccept_;
    // The Box's answer row. messageBoxChoices_ is true only for a box raised by showConfirm, because only a
    // question has answers: a box raised by showMessage keeps the single-action path (Cross dismisses it) and
    // draws no live selection. messageBoxChoice_ is the answer, not the drawn slot - 0 = No (the console's
    // default and where every box starts), 1 = Yes - and boxChoiceSlot() maps it to the {Yes, No} draw order.
    bool messageBoxChoices_=false;
    BoxChoice messageBoxChoice_=BoxChoice::No;
    // XMB_DIALOG_LOG=1 names each box event (raised, moved, answered, cancelled) on stderr, so a scripted run
    // can prove the choice it made without reading pixels. Off by default, like the other per-surface logs.
    bool dialogLog_=false;
    QString mediaTitle_,renderer_;
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
    void storeResumePosition(const QString& path,qint64 positionMs,qint64 durationMs,const QString& title);
    void clearResumePosition(const QString& path);
    // ---- Continue Watching (audit/MEDIA-CONTROLS.md) ----
    // media/resume holds one JSON object, {id: {path, ms, dur, title, at}}, where id is the same
    // truncated SHA-1 of the path the earlier {id: milliseconds} map used. The old shape is still read
    // (and is what the migration finds on an upgraded data directory); a record with no path is a
    // legacy entry that has not been resolved yet and is never listed.
    void migrateResumePoints();                  // once, at load: resolve or drop pathless entries
    QVector<Entry> continueWatchingRows() const; // newest first, at most 20, nothing older than 30 days
    void openContinueWatching();
    void reloadContinueWatching();
    void removeContinueEntry(const QString& id,const QString& title);
    int rememberedResumeCount() const;
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
