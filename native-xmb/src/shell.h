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
#include "steam_accounts.h"
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
    JellyfinSignInField, JellyfinSignInSubmit, JellyfinSignInCancel, JellyfinSignInRemember,
    // Settings > System Settings > Restore Default Settings: a real, confirm-gated action that puts every
    // preference this build owns back to its default for the active profile. It is app state only - no host
    // setting, library path or profile is touched - and the confirmation box names exactly what is reset.
    ResetPreferences,
    // Users > Steam Account (FIDELITY-BEHAVIOUR U4): the two-step account-switch surface the data layer
    // (steam_accounts.h) has carried with no UI. SteamAccounts opens the page, SteamAccountSwitch is one
    // account's row and runs the existing handoff plan for it. There is no console analogue: the 4.93 XMB
    // binds a PSN account, and no local frame shows a console account-switch page, so the surface is this
    // app's ordinary page of rows (see openSteamAccounts()).
    SteamAccounts, SteamAccountSwitch,
    // One group folder of a page the item menu's Group Content rebuilt (an "A".."Z"/"#" letter row). Its
    // value is the group's key; activating it pushes that key's rows. It exists only inside a grouped page.
    GroupFolder };
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
    // This row's artwork is an icon-sized picture even though it is loaded from a file (container "file"):
    // the Photo column's `Photo Gallery` row draws the firmware's own hakoniwa.png, which is 180x180, where an
    // installed title's ICON0.PNG is a wide card. The view's wide-tile rule reads this so the gallery keeps the
    // icon box the console gives it (the one-line view hunk in audit/FIDELITY-SURFACES.md's appended section).
    bool iconSized=false;
    // The value drawn on the right of a focused row, above every other source the view consults. The sign-in
    // page's three fields use it (server URL, user name, and the password as dots); every other row leaves it
    // empty, so the view falls through to Shell::valueText() exactly as before and no existing frame changes.
    QString fieldValue;
    // The Settings page a row belongs to ("system", "network", "device", ...), empty on every other row. It is
    // what lets the shell resolve a row's live value and its information page through host::rowValue /
    // host::rowDetail - the machine half of the settings source layer - without every call site naming the
    // page again. Set by the Settings page builders in settings_menu.cpp only.
    QString settingsPage;
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
    PlayFromBeginning,
    // Group Content's level and its two answers (msg_folder_class, msg_title_folder "By Title", msg_all "All"),
    // offered only where optionGroupable() holds. They rebuild the open page's row vector; the page keeps the
    // ungrouped rows so All is reversible and no rescan is needed.
    GroupMenu, GroupByTitle, GroupAll };
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
    // The two flags the firmware's item menu draws and this build's shell did not carry
    // (FIDELITY-SURFACES 6.3/6.5/6.6, re-measured on frames/menu-options-open.png this pass):
    //   submenu    the row opens a second level, so the view draws the right-pointing chevron at x 1573..1583
    //              on this row's centre line. True on Sort By and Group Content.
    //   gateBadge  the row carries the small purple square the frame shows after its label (13x14 px at
    //              x 1517..1529, y 345..358, fill #BF4EA6 with a dark inner rect). True on Group Content only.
    // Neither is a capability claim: both are what the settled 4.93 frame draws on these two rows.
    bool submenu=false;
    bool gateBadge=false;
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
    // The tenth column (FIDELITY-ROOT-BAR C1/#5). The 4.93 console draws NINE categories and no TV / Video
    // Services column: category_tv.xml's root view lists only content providers (the welcome store query, the
    // in-game exit and the tv content list), so with nothing registered the column has no rows and the bar
    // never shows it. This build's TV column is where imported channel apps (and the Jellyfin surface) live,
    // so it stays on by default; `appearance/tvColumn` = false drops the category and the bar reflows to the
    // console's nine. That is the console-parity setting, and the only one that changes the category count.
    // An unset or unreadable value means ON, so a data directory that never wrote the key keeps today's XMB.
    bool tvColumnVisible() const;
    // The PS3 theme in use (Theme Settings > Theme), decoded in memory from its .p3t: empty path = the system's own.
    QString themePath() const { return theme_.path; }
    const QImage& themeBackground() const { return theme_.background; }
    QImage themeIcon(const QString& id) const { return id.isEmpty()?QImage{}:theme_.icons.value(id); }
    bool fullscreen() const { return preferences_.value("display/fullscreen").toBool(); }
    bool photoFill() const { return preferences_.value("photo/fit").toInt()==1; }
    bool photoOpen() const;
    bool slideshowActive() const { return slideshow_.isActive(); }
    // The console's busy state (audit DIALOGS-FIRMWARE 2.8 page_busy; FIDELITY-BEHAVIOUR N10/N12/X5).
    // True from the moment a list scan is asked for until its rows land, and while a settings write's
    // proceed stage holds the page. Navigation is refused while it is true (N11: the console does not treat
    // Left/Right as a cancel), and View draws page_busy from this one flag.
    bool busy() const { return loading_ || busy_; }
    QString status() const { return status_; }
    // The determinate form of the busy page: 0..1 while the operation publishing it knows its total, -1
    // (the default) for the indeterminate wait page the console frame shows.
    double busyProgress() const { return busyProgress_; }
    // The D8 busy_icon sweep, 0..1 per turn. -1 draws no ring: the measured wait page has none and
    // page_busy's children carry no rect in the RCO, so no frame can place one.
    double busySpinnerPhase() const { return busySpinnerPhase_; }
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
    // ---- The writable-row chain (audit/SETTINGS-FIRMWARE.md 4; FIDELITY-BEHAVIOUR I77) ----------------
    // In the console a settings write is a chain, not a frame: the row's own page (page_*_top) -> an ask
    // page (*_ask) -> the proceed page (*_proceed) while the write runs -> the result page (*_finish). This
    // build committed and popped in one frame. The chain below runs the same three stages on the surfaces
    // this app already draws - Ask is the two-answer box (D1), Proceed is the page_busy state, Finish is the
    // page message (D3/D4) - and every sentence is read from sysconf_plugin where that plugin has one.
    // The stage is exposed so a scripted run (and the capture sidecar's reader) can name what is on screen.
    QString settingsWriteStage() const;   // "ask"/"proceed"/"finish-ok"/"finish-failed"/"" 
    QString settingsWriteKey() const { return settingsWrite_.key; }
    QString settingsWriteResult() const { return settingsWrite_.result; }
    // ---- Media: the Display key (audit/MEDIA-PANELS-FIRMWARE.md 3.4/3.6; FIDELITY-BEHAVIOUR I48) -------
    // The video container labels SELECT "Display" (msg_display_display_with_icon ends in U+F88E, and
    // friendim_plugin's msg_select_gaiji is the same code point). This flag is that state: START and the
    // in-player chrome keep the time bar and the hint strip up while it is set. View draws from it.
    bool mediaDisplayPinned() const { return mediaDisplayPinned_; }
    void toggleMediaDisplay();            // SELECT: pin/unpin the player's own display
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
    bool jellyfinSignInOpen() const;    void setJellyfinSignInField(const QString& field,const QString& value);
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
    // "jellyfinFolder", "downloads", "continueWatching", "steamAccounts", "folder", "page" or "root".
    QString pageKind() const;
    // ---- Users > Steam Account (FIDELITY-BEHAVIOUR U4) -------------------------------------------------
    // The page lists the accounts scanSteamAccounts() reports for the installed client, marks Steam's own
    // active record (or, when the client is signed out, the account its own auto-login marker names), and a
    // selection runs steamSwitchPlanFor()'s two-step handoff: a box per step, then the two launches, then a
    // result page carrying prompt.outcomeText or the real failure. No credential is read, written or logged;
    // nothing starts before both steps are confirmed (SteamSwitchConfirmation).
    void openSteamAccounts();
    bool steamAccountsOpen() const;
    // The accounts on the open page, for the dump and the scripted check.
    int steamAccountCount() const { return steamAccounts_.size(); }
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
    // True while a second level (Sort By, or Group Content's `By ...`/All list) is up. The settled frame of an
    // open group list (frames/menu-submenu-group-content.png) marks it with a left-pointing chevron beside the
    // focused row's label column: the view draws that from this flag (see the returned view hunk).
    bool optionsNested() const { return optionsNested_; }
    // The list-level rows this build can offer on the selected row: what the item menu's group 0 holds.
    // Exposed so a scripted run can see the structure without reading the panel log.
    QStringList optionsStructure() const;
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
    // The settings-chain proceed stage holds the busy page (see busy()). Separate from loading_ so a scan
    // and a write can never clear each other's state.
    bool busy_=false;
    // The two values the busy page's determinate form reads (see busyProgress()/busySpinnerPhase()); both
    // stay -1 until an operation publishes a total or a phase.
    double busyProgress_=-1.;
    double busySpinnerPhase_=-1.;
    QElapsedTimer busyClock_;
    // The store's latest refusal, from Preferences::failed (the signal carries the reason for every case; the
    // store's error() only keeps the read/format ones). The write chain's failure page shows this text.
    QString lastPreferencesError_;
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
    // `grouped` is a page the item menu's Group Content level rebuilt into letter folders: `groupedSource` keeps
    // the ungrouped rows so All can put them back without a rescan, and the folders' rows are the same entries.
    struct Page { QString title; QVector<Entry> entries; int selection=0; QString directory; bool jellyfin=false,jellyfinViews=false,jellyfinSearch=false; QString jellyfinParent; int jellyfinStart=0; bool downloads=false; bool continuePage=false; bool signIn=false; bool steamAccounts=false; bool grouped=false; QString groupKey; QVector<Entry> groupedSource; };
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
    // Group Content: the list-level row the settled frame draws between Sort By and the item actions, and the
    // level it opens. `optionGroupable` is true on a page whose rows carry titles this build can group by first
    // letter (every ordinary list, never a provider or form page); `buildGroupContentMenu` is the level's own
    // row set; `groupPageBy` rebuilds the open page (GroupTitle / GroupAll).
    bool optionGroupable() const;
    void buildGroupContentMenu();
    void groupPageBy(OptionCommand command);
    void openGroupFolder(const Entry& entry);
    QString groupLetter(const QString& title);
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
    // The TV column's presence: the category is built like every other one and then stashed or restored here,
    // so hiding it is one erase (and the bar, the navigation indices and the entry dump all follow categories_).
    void syncTvColumn();
    Category tvCategory_;
    bool tvColumnStashed_=false;
    // The media columns, resolved from category_video/music/photo.xml on every start (the same mechanism as the
    // Network column). Declared here because they are built once from the firmware definition, not per frame.
    void resolveMediaColumns();
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
    // The Display key's state (see mediaDisplayPinned()); cleared when playback ends.
    bool mediaDisplayPinned_=false;
    // ---- Steam accounts page state (Users > Steam Account) --------------------------------------------
    QVector<SteamAccount> steamAccounts_;
    QString steamAccountsError_;
    SteamSwitchPrompt steamSwitch_;
    SteamSwitchConfirmation steamSwitchConfirmation_;
    QString steamSwitchId_;
    bool steamSwitchHold_=false;      // XMB_STEAM_SWITCH_HOLD=1: log the handoff, start nothing (a test hook)
    bool steamPrompt_=false;          // one of the two prompt boxes is up
    void steamAccountSelected(const QString& steamId64);
    void steamSwitchNextStep();       // push the next Box of the two-step prompt
    void steamSwitchConfirmed();      // the model advanced one step: push step 2 or run the handoff
    void steamSwitchRun();
    void steamSwitchResult(bool ok,const QString& error);
    void steamSwitchCancel(const QString& why);
    void openSettings(const QString& page);
    void openChoices(const QString& key,const QString& title);
    // ---- The settings write chain ---------------------------------------------------------------------
    // One write in flight. The Ask stage is a Box (its accepted callback is this object's own, so the two
    // answers are the two branches of Shell::activate's Box path); the Proceed stage raises the busy page;
    // the Finish stage is a Page message that names the new value or the write's real error.
    enum class WriteStage { None, Ask, Proceed, FinishOk, FinishFailed };
    struct SettingsWrite {
        WriteStage stage=WriteStage::None;
        QString key,label,valueText,result;
        QVariant value;
        QString askBody,proceedText,okText,failText;
        bool slow=false;      // the write gets the Proceed stage
        bool popBand=false;   // a band was open: the finish returns to the row list
        bool host=false;      // the write goes to the computer, not the app's store
        bool resetAll=false;  // Restore Default Settings: one atomic multi-key write
        bool themeToSystem=false; // "Original" over a wallpaper: clears the theme AND the background mode
    };
    SettingsWrite settingsWrite_;
    void beginSettingWrite(const Entry& entry);
    void beginResetWrite();           // Restore Default Settings: the console's own top|proceed|finish chain
    void startSettingsWritePage();    // Ask accepted (or skipped): Proceed, then runSettingWrite()
    void runSettingWrite();           // the write itself, then the Finish stage
    void finishSettingWrite();        // the Finish page was dismissed: back to the row list
    void cancelSettingWrite(const QString& why);
    // Restore Default Settings: every key this build stores, set back to the default Preferences documents
    // (preferences.cpp defaultValue). Called only from the confirmed box, and logged with the count.
    void restoreDefaultSettings();
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
