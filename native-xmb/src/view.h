#pragma once
#include "assets.h"
#include "coldboot.h"
#include "gallery_controller.h"
#include "hdr.h"
#include "particles.h"
#include "icons.h"
#include "ribbon.h"
#include "rsx_program.h"
#include "shell.h"
#include "ps_watcher.h"
#include "photo_viewport.h"
#include "media_controls.h"
#include "text_renderer.h"
#include "osk_layout.h"
#include "rco_objects.h"
#include "media_keys.h"
#include "game_capture.h"
#include <array>
#include <functional>
#include <QDateTime>
#include <QSet>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QElapsedTimer>
#include <QTimer>
#include <QOpenGLTexture>
class QAbstractNativeEventFilter;
class QLabel;

namespace xmb {
struct GalleryState;
class View final:public QOpenGLWidget,protected QOpenGLFunctions_3_3_Core {
public:
    View(Shell& shell,Assets& assets,QWidget* parent=nullptr);
    ~View() override;
    static double dayLight(double hour);
    // The XMB's moves are paf animations: 200 ms in accel mode 5 (vsh.elf PhXmBar focus moves FUN_003ad3bc /
    // FUN_003ad728; paf animation setup FUN_003981ec and evaluator FUN_00398748). Mode 5 closes a fixed fraction
    // k of the remaining distance every 60 Hz frame, k from a table by the duration in frames, and ends only when the
    // stepped value reaches 1 (no landing at the duration; FUN_00398540, 0x3988f0). A new target restarts the curve
    // from the current value.
    // Mode 4 is the same per-frame form with its own k table (the page content channel, PAGE-MOVE 4c).
    // `delayMilliseconds` holds the value at `from_` for that long before the curve starts. paf's own ease state
    // has this phase too (FUN_003987dc state 0 waits until the clock passes state+0x18), and the animate wrapper's
    // f2 is that delay (sysconf's idle stagger passes 60..390 ms); the options panel's start offset uses it as well.
    struct PafEase {
        double value=0;
        void retarget(double target,double milliseconds=200,int mode=5,double delayMilliseconds=0);
        void advance(double seconds);
        bool running() const { return running_; }
    private:
        double from_=0,to_=0,progress_=1,elapsed_=0,delay_=0,k_=0,growth_=1.000001;
        bool started_=false,running_=false;
    };
    static constexpr double moveMilliseconds=200;
    // The notification hide is the firmware's own 100 ms fade (basic_plugins.prx DAT_00047f94 = 100.0,
    // NOTIFICATIONS.md 11.2). The slide-in keeps the shared 200 ms mode-5 curve; only the exit shortens.
    static constexpr double notificationHideMilliseconds=100;
    // The page (depth) move's two channels (audit/PAGE-MOVE.md 14.1, 15, and the 2026-11-05 runtime trace):
    //   pageMilliseconds, mode 5 - the page content (the icon's 262 px travel and the item column's entry).
    //     An RPCS3 run with every paf ease setup logged (FUN_003981ec) shows no mode-4 call during a page open or
    //     close: all are mode 5, 200 ms for the bar nodes (a few 300). The burst's 0.762/frame (0.775 after the
    //     tracker bias) is mode 5 at 200 ms (0.7794), not 300 (0.847); its 18 visible frames are the ease's own
    //     tail (no landing at the duration). This was 300 ms mode 4, a fit to the frame count.
    //   barMilliseconds, mode 5 - TRACED: the firmware's own bar re-layout on a page change is 200 ms, mode 5
    //     (xmb_plugin state+0x520 = 200.0, every bar animator passes 5; PAGE-MOVE 14.1). It drives the bar
    //     layer: the recede distance/projection, the bar's own focus row and the neighbours' brightness.
    static constexpr double pageMilliseconds=200;
    static constexpr double barMilliseconds=200;
    // The page move's two channels also carry the console's measured CLOSE hold. Measured frame by frame on the
    // 642-frame burst (C:\dev\out\rpcs3-xmb-cap\burst-png; PgUp at 2706.9 ms, 60 Hz renders): the page icon is
    // untouched at 2743 and has moved 62 of its 262 px at 2760 - exactly one 60 Hz curve frame (0.226 x 262 =
    // 59). So the ease's first stepped frame is the 2760 render and its start lies in (2743, 2760], i.e. the hold
    // is 37..54 ms after the key. 49 is used: with PafEase's elapsed clock starting at the paint before the key
    // (up to one frame early), 49 lands the native's first rendered step within 7 px of the console's +62 at
    // +57 (measured: 41 puts it 22 px ahead, 57 lags 36 px behind). The audit's "first motion +57" is the frame
    // the console's first step is seen on, not the hold's length. The close's per-frame retention is 0.762, the
    // open's own shape within the tracker's -0.014 bias, so the curve is NOT re-fit: the close is the same
    // content ease as the open with this hold in front of it.
    static constexpr double pageCloseDelayMs=49;
    // A Settings page open (sysconf_plugin) holds still for the firmware's job chain (PAGE-MOVE "The page-open
    // job chain", 2026-11-06, RPCS3 syscall trace): the press frame starts a ScePafJob thread and each later main
    // loop frame joins the finished job and starts the next; the fifth job loads and starts sysconf_plugin and
    // reads its resources, and the frame that joins it starts the page eases. At one job per 60 Hz frame that is
    // 5 frames before the page can move. TRACED floor only: the fifth job's load and resource read (0.38 s on the
    // rig) is I/O and emulation speed, not a firmware constant, and is not added.
    static constexpr double pluginPageOpenFloorMs=5*1000./60;
    // sysconf_plugin's idle timer (FUN_000a7948, DAT_0014bb78 = 3000.0) and the text level the other rows ease to
    // when it fires (FUN_000a70e4, DAT_0014bb60 = 0.2; measured 0.197 of the label's ink on the console burst).
    static constexpr double settingsIdleMs=3000;
    static constexpr double settingsIdleDim=.2;
    // The item-level Triangle options panel's entrance (UI-ELEMENT-GAP-AUDIT O6, measured [M]): the open
    // starts 115..150 ms after the key and ramps 245..255 ms (at rest 360..410 ms); the close starts 9 ms
    // after the key and ramps 200 ms. The mid-points of the measured ranges are used. The exit used to be a
    // hard cut because the panel's draw was gated on Shell::optionsOpen(); the content is now latched
    // (OptionsShown below), so the measured 9 ms + 200 ms close has something to draw.
    static constexpr double optionsOpenDelayMs=130;
    static constexpr double optionsOpenMs=250;
    static constexpr double optionsCloseDelayMs=9;
    // Renders the background offscreen at reference times and writes the same statistics measured from the
    // reference videos (band colours by clock time, ribbon crest over 40 s). Returns false on GL failure.
    bool measureReference(const QString& path);
    void setBackgroundClock(const QDateTime& when) { backgroundOverride_=when;update(); }
    // Fixed animation time (seconds) for deterministic A/B captures; -1 follows the real clock.
    void setRenderClock(double seconds) { secondsOverride_=seconds;update(); }
    // Diagnostic focus age for reproducible glow/idle-fade captures; navigation still animates normally.
    void setFocusTime(qint64 milliseconds) { focusTimeOverride_=milliseconds;++uiRevision_;update(); }
    // The 4.93 cold-boot intro before the XMB (coldboot.h); off for measurement and scripted runs. Set before show.
    void setIntroEnabled(bool enabled) { introEnabled_=enabled; }
    void setHealthWarningEnabled(bool enabled) { coldboot_.setNoticeEnabled(enabled); }
    // Verification taps dispatch synchronously, independent of foreground focus and the physical input poll.
    void replayKeyTap(int key);
    // Photo Gallery (gallery_chrome.cpp, audit/PHOTO-GALLERY.md). Cross on Photo > Photo Gallery opens it; --gallery-root
    // replaces the Photo library folder preference as the root (checks and scripted runs). galleryOpen() is true while
    // its area is up.
    void openGallery();
    void closeGallery();
    bool galleryOpen() const;
    void setGalleryRoots(const QStringList& roots) { galleryRoots_=roots; }
    // Verification taps for the PS button (no physical controller in scripted runs). They drive the
    // same global watcher state machine a physical press does, including the 1 s hold.
    void replayPsTap();
    void replayPsHold();
    // Desktop Mode (desktop_mode.h): the global PS/Guide watcher, which the mode listens to while the window is hidden, and
    // the suspended state that slows the 16 ms controller timer to a low-rate idle tick (the watcher is slowed by the mode).
    PsWatcher& psWatcher() { return psWatcher_; }
    void setSuspended(bool suspended);
    // Counted by the frame signal; the mode reads it to prove the hidden window renders nothing.
    qint64 framesSwapped() const { return framesSwapped_; }
    // The couch volume path, reachable from --script so a capture can pin an exact level. These are the
    // same functions the pad's L2/R2, the media keys and the native WM_APPCOMMAND filter call.
    void setMasterVolumePercent(int percent);   // VOLSET=<0..100>
    void restoreMasterVolume();                 // VOLRESTORE: the level and mute state read at start-up
    // Remote and media keys (PHASES H10, audit/INPUT.md): one entry for WM_APPCOMMAND, VK_MEDIA_*, Qt::Key_Media*
    // and the MCE shortcuts. False = not taken (the message keeps its normal Windows routing).
    bool remoteKeyAction(RemoteKey key,const QString& source);
protected:
    void initializeGL() override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    Shell& shell_;
    QLabel* artworkCredit_ = nullptr;
    void resizeEvent(QResizeEvent* event) override;
    Assets& assets_;
    QOpenGLShaderProgram background_;
    GLuint vao_=0;
    bool glReady_=false;
    bool replayingInput_=false;
    QTimer timer_;
    QElapsedTimer clock_;
    // Opt-in, bounded in-memory frame trace; written only on shutdown to avoid per-frame disk I/O.
    struct FrameSample { qint64 at,cpu; bool swap,rebuilt,moving; };
    QString frameTracePath_;
    qint64 sceneNs_=0,iconNs_=0; // XMB_TEXT_COST: the last frame's drawScene and glass-icon pass, CPU ns
    QVector<FrameSample> frameTrace_;
    void saveFrameTrace();
    qint64 previousFrameNs_=0,reportAt_=0;
    // PS button: the global watcher (independent of window focus) and the short controller-indicator
    // window a tap opens.
    PsWatcher psWatcher_;
    qint64 framesSwapped_=0;
    // The tap indicator's window is the console's own, measured on the PS-tap burst (MANIFEST 4):
    // 155 ms after the press it starts to fade in, full 66 ms later, held ~2.0 s, gone 66 ms after that
    // (2288 ms after the press). psHudAt_ is the press instant, psHudUntil_ the hard end.
    qint64 psHudAt_=0,psHudUntil_=0;
    double psHudOpacity(qint64 now) const;
    // The PS-hold menu's own envelope, measured on the console's hold burst (PARITY-SCORECARD 16.3,
    // C:\dev\out\rpcs3-gaps-2026-10-28\burst\ps-menu-entries, 206.6 fps, PS pressed 150 ms into the
    // burst): the frame is untouched while the XMB is up, goes FULLY black 1052 ms after the press, both
    // entries' text reaches full opacity at 1326 ms, and the selected row's bloom keeps growing until
    // 1840 ms. psPressAt_ is the press instant the watcher reported; 0 means "no press seen".
    qint64 psPressAt_=0;
    // The busy page's own clock (page_busy; DIALOGS-FIRMWARE 2.8), latched on the busy edge so a phase
    // computed from it is stable under a --render-clock capture. The dialogs pass's surface draws no ring by
    // default (the measured wait page has none and page_busy's children carry no rect), so nothing reads these
    // yet; they are here for the day the shell publishes a spinner phase, exactly as the returned hunk asks.
    bool busyWas_=false;
    qint64 busySinceMs_=0;
    static constexpr double psHoldBlackMs=1052;    // the whole frame is black from here after the press
    static constexpr double psHoldTextStartMs=1279;// the entries' first ink
    static constexpr double psHoldTextMs=1326;     // both entries' text at full opacity
    static constexpr double psHoldBloomMs=1840;    // the selected row's bloom at its resting strength
    double psHoldTextAlpha(qint64 now) const;
    double psHoldBloomAmount(qint64 now) const;
    bool psPressActionable_=false;
    bool titleRunning() const;
    bool xmbOwnsForeground() const;
    void psPressed();
    void psHeldEvent();
    void psTappedEvent(qint64 durationMs);
    void psReleasedEvent(qint64 durationMs);
    void bringXmbForward(const QString& reason);
    // ---- In-game XMB (plan F5, audit/INGAME-XMB-DESIGN.md steps 4-6) ----
    // enterInGame(): record the title's window, classify it, capture one frame through DWM (never injected), tell the
    // shell the XMB is now in-game (rows, help line) and raise the XMB. returnToGame(): the reverse; hands the
    // foreground back to the recorded window. inGameBack(): Circle at the column root is "Back to Game Screen".
    void enterInGame(const QString& reason);
    void returnToGame(const QString& reason);
    void syncInGameWindow();
    bool inGameBack();
    void drawInGameBackdrop(int width,int height);
    void startLiveCapture();
    void stopLiveCapture();
    void captureLiveFrame();
    QTimer liveCaptureTimer_;
    bool liveCaptureActive_=false,liveCapturePending_=false;
    quint64 liveCaptureGeneration_=0;
    QImage inGameBackdrop_;           // the captured game frame, empty when capture was black/failed/unsupported
    quintptr inGameHwnd_=0;           // the title's window recorded at the PS press
    bool inGameWindowActive_=false;  // includes the plain-dark overlay when capture found no window
    quint32 inGamePid_=0;
    GameWindowMode inGameMode_=GameWindowMode::Invalid;
    void reassertForeground(const QString& reason);
    void activatePsMenuSelection();
    quint64 frames_=0;
    double categoryPosition_=6;
    int previousCategory_=6;
    struct Column { QVector<Entry> entries; int selection=0; double position=0; PafEase ease; };
    PafEase categoryEase_,depthEase_,fadeEase_,barEase_;
    // Item-level Triangle options panel: its own open amount (slide-in) and focus position, animated with the
    // same paf curve as the settings panel. Drawn while Shell::optionsOpen() OR while its close ramp is still
    // running, from the latched content below.
    PafEase optionsPanelEase_,optionsOpenEase_;
    bool optionsWasOpen_=false;
    // The panel's content, latched across its close (PAGE-MOVE 15.4). Shell drops the options the moment the
    // closing Triangle lands, so the draw used to disappear in one frame while the measured close ramps 200 ms
    // (O6). The latch keeps the last titles/heading/rows/selection so that ramp has content to fade; `player`
    // records which of the two panel shapes it was (the in-player panel or the item menu).
    struct OptionsShown {
        QStringList titles;
        QString heading;
        QVector<MenuOption> rows;
        int selection=0;
        bool player=false,valid=false;
    };
    OptionsShown optionsShown_;
    bool optionsDrawn() const { return optionsShown_.valid && optionsOpenEase_.value>0.001; }
    // The console's notification pop-up (Shell's queue). The ease is its presence on screen: it slides and
    // fades in over 200 ms (paf mode 5, the XMB's own move curve) and back out the same way. The last content
    // is kept while it leaves, because Shell has already dropped the entry by then.
    PafEase notificationEase_;
    struct NotificationShown { QString title,detail,icon; };
    NotificationShown notificationShown_;
    bool notificationTextureWarned_=false;
    void drawNotification(QPainter& painter,double amount);
    // The "Triangle : Options" operation guide (audit/FIDELITY-ROOT-BAR.md 6.1 update 3): its opacity at this
    // focus time, 0 outside the traced 1000 ms delay / 200 ms fade / 3000+200 ms life window, and its draw.
    double optionHintAmount() const;
    // user_plugin's page_create (audit/FIDELITY-SURFACES-MAP.md S25): the prompt, the name field and OK. Field
    // focus is 0, OK is 1; Cross on the field opens the OSK, Cross on OK creates the user, Circle goes back.
    // renameId set: the page renames that user (Information on a user row) instead of creating one.
    struct UserCreatePage { bool open=false; QString name; int focus=0; QString renameId; };
    UserCreatePage userCreate_;
    // playlist_plugin's two-pane editor (expage_select_list / expage_edit_list; audit/PLAYLIST-EDITOR.md). Pane 0
    // lists every local file of the playlist's kind (edit_src_lst), pane 1 the playlist (edit_dst_lst). Cross
    // adds from pane 0, picks a row up in pane 1 and drops it; Square deletes in pane 1; Triangle in pane 0
    // opens the Options plate (Add All); Circle asks msg_quit_playlist_edit_ask. Every change saves through
    // Shell::playlistEdit* at once; a pick-up is previewed in `work` and saved as one move on the drop.
    struct PlaylistEditor {
        bool open=false;QString id,name,type;QStringList library,items,work;
        int pane=0,libSel=0,libTop=0,listSel=0,listTop=0;
        bool picked=false;int pickFrom=-1;
        bool optionsOpen=false,confirmQuit=false;int confirmFocus=0;   // confirmFocus 0 = No, 1 = Yes
        QString status;
    };
    PlaylistEditor plEdit_;
    void openPlaylistEditor(const QString& id);
    void drawPlaylistEditor(QPainter& painter);
    // 0 up, 1 down, 2 left, 3 right, 4 cross, 5 circle, 6 triangle, 7 square; true when the editor took it.
    bool playlistEditorInput(int action);
    void drawUserCreatePage(QPainter& painter);
    bool userCreateInput(int action); // 0 up, 1 down, 2 cross, 3 circle; true when the page took it
    void drawOptionHint(QPainter& painter,double amount,const QString& textOverride={});
    // The active sidecar subtitle cue (Shell::subtitleCueText): centred in the lower third, above the
    // media controls, drawn through the GL glyph atlas with a dark outline so it stays legible over a
    // bright frame. audit/SUBTITLES.md.
    void drawSubtitle(QPainter& painter,const QString& cue);
    // A 64x64 RCO tile (the notification plaque) stretched to any size without turning its rounded corners
    // into ellipses: the corners keep their pixels, the edges and centre stretch.
    // `sourceInset` drops that many pixels of the tile's own border from the source grid (the info bar's
    // tile carries a 1 px, ~4% alpha margin row/column that must not be drawn as part of the plate).
    void drawNineSlice(QPainter& painter,const QString& container,const QString& texture,const QRectF& rect,qreal inset,qreal opacity,qreal sourceInset=0.);
    QVector<Column> columns_;
    QVector<Column> outgoing_;
    int outgoingCategory_=6;
    double categoryFade_=0;
    // Time since the focused row (category, depth, selection) last changed: label glow phase and idle label fade.
    QElapsedTimer focusClock_;
    qint64 focusTimeOverride_=-1;
    qint64 focusTime() const { return focusTimeOverride_>=0?focusTimeOverride_:(focusClock_.isValid()?focusClock_.elapsed():0); }
    qint64 focusKey_=-1;
    QString focusedEntryId_;
    // ICON0 is row art; PIC1 is a separate, delayed background. Timing is a
    // native fit until a frame-timed firmware capture establishes the constants.
    static constexpr qint64 artworkDwellMs=2000;
    static constexpr double artworkFadeMs=350.;
    QString focusedBackgroundPath_;
    QImage focusedBackground_,departingBackground_;
    quint64 focusedBackgroundRequest_=0;
    double focusedBackgroundOpacity_=0.,departingBackgroundOpacity_=0.;
    // A Jellyfin page's backdrop (Shell::pageBackdrop()) rides the same slot when the focused row has no
    // background file. It belongs to the page, not the row, so moving between rows keeps it: its dwell runs
    // from when the page's art arrived instead of from the last row move.
    bool focusedBackgroundIsPage_=false;
    QElapsedTimer pageBackdropClock_;
    QString focusedBackgroundKey(QString& path);
    void syncFocusedBackground();
    void drawBackdrops(QPainter& painter);
    double depthPosition_=0;
    // The bar layer's own depth progress (barMilliseconds/mode 5) next to the page content's (pageMilliseconds/
    // mode 5). Both are 200 ms mode 5 since the 2026-11-05 trace; they stay separate eases because they are
    // separate firmware animations. At rest both equal shell_.depth(), so nothing at the canonical rest frame moves.
    double barDepthPosition_=0;
    int previousDepth_=0;
    // The depth the previous painted frame reported, so the eases can tell an opening page from a closing one
    // (the close holds ~49 ms before its first step; `previousDepth_` is already the new depth by paint time).
    int depthSeen_=-1;
    void syncColumns();
    // Historical diagnostic projection only (XMB_OVERSCROLL=<0..8>).
    // Normal navigation keeps the column anchored at either list boundary.
    void setColumnSink(int presses) {
        if(presses<0)presses=0;
        if(presses==columnSink_)return;
        columnSink_=presses;++uiRevision_;update();
    }
    static constexpr double overscrollStep=40.;
    int columnSink_=0;
    // ---- Phase 11.3: the settings choice band -------------------------------------------------------
    // The committed choice model (settings_menu.h) owns the lists; the draw pass needs only the
    // firmware's own labels in their authored order, and each item's value so the row the model has
    // selected can be found again when the RCO's authored order differs from choices()'s (Date Format
    // and Time Format are authored in another order - SETTINGS 11.3/11.6). Resolved once per process,
    // on the first settings choice that draws, and cached by the Preferences key the row writes.
    // `source` says where the labels came from: "rco" (the firmware's own page), "capture" (Theme, whose
    // RCO page is empty and whose labels are the 2026-10-28 capture's), "native" (this build's list for
    // a row with no firmware page) or "none" (no page at all). Only rco/capture draw the band; the rest
    // keep this build's options panel, with a logged line, so no function is lost.
    struct ChoiceBand {
        QString setting,pageId,rowId,rcoPage,source,note;
        QStringList labels;
        QVector<QVariant> values;
        QVector<bool> writable;   // false: a firmware item this build cannot commit yet
        bool scrollable=false;
        // Phase 11.4: a Theme run-time list. `swatch` marks a colour chip (drawn from `colours`, no label),
        // `submenu` the firmware's right-hand chevron row (`Brightness`), `notes` the per-item reason a
        // disabled item carries, and `windowRows` the number of rows the captured list shows at once (the
        // Color list's 12; 0 keeps every other band's own rule). The note line is this build's own.
        QVector<bool> swatch,submenu;
        QVector<quint32> colours;
        QStringList notes;
        int windowRows=0;
    };
    QHash<QString,ChoiceBand> choiceBands_;
    QSet<QString> choiceBandsLogged_;
    bool choiceBandsBuilt_=false;
    const ChoiceBand* choiceBandFor(const QString& setting);
    // The drawn row that owns the model's selection: the item whose value the model would commit (so the
    // focused label and the committed value cannot disagree), else the item the row's own displayed value
    // names (the time zone), else the model's position.
    int choiceBandFocus(const ChoiceBand& band,const QVector<Entry>& model,int selection,const QString& currentText) const;
    QImage mediaFrame_;
    PhotoViewport photoViewport_;
    MediaControls mediaControls_;
    bool previousMediaOpen_=false;
    QString previousMediaTitle_;
    // ---- Couch volume: the PC's master volume, the pad's L2/R2, the Windows media keys, and the HUD ----
    // The firmware's own volume display is one page holding a single level meter (videoplayer_plugin.rco
    // videoplayer_volume_page / videoplayer_volume_level_meter, and the same pair in audioplayer_plugin.rco
    // and explore_plugin_full.rco volume_page / volume_level_meter). The meter is paf's PhLevelMeter, drawn
    // from system_plugin.rco's default level-meter art - tex_default_lvmeter_on/_off/_focus, 20x40 segments,
    // over tex_default_lvmeter_bg 48x48 - with no caption, no percentage, no plate and no border
    // (audit/MEDIA-PANELS-FIRMWARE.md 5.2-5.4). The widget's rect and its segment count are NOT in any
    // container, so both are FITS here; the fade is the traced 150 ms / 0 delay (explore_plugin.prx
    // 0x270a00 / 0x270a04). MediaControls still owns the visible window.
    MediaControls volumeHud_;
    static constexpr int volumeMeterSegments=10;              // FIT: audit 5.4, the count is spec-only
    static constexpr double volumeMeterFadeMs=150.;           // FD: 0x270a00, delay 0 at 0x270a04
    int volumeLevel_=-1;             // last level Windows reported; -1 when the endpoint is unavailable
    bool volumeMuted_=false;         // Windows' endpoint mute state as last read
    int volumeStart_=-1;             // start-up level and mute state, put back by restoreMasterVolume()
    bool volumeStartMuted_=false;
    bool triggerHeld_=false;         // an L2/R2 press is down, for the one-step-per-press seek edge
    qint64 volumeHudAt_=0;           // when the HUD's 3 s interval started, for the opt-in fade trace
    bool volumeHudVisible_=false;    // for the opt-in trace of the HUD's own expiry
    bool volumeWarned_=false;        // one line only when the endpoint cannot be read
    std::unique_ptr<QAbstractNativeEventFilter> volumeKeys_;
    bool volumeInputBlocked() const;                       // a surface that owns input or the frame is up
    bool volumeKeyAction(int action,const char* source);   // 1 up, 2 down, 3 mute; false = key not taken
    void volumeStep(int direction,const QString& source);  // one 5% step through the host endpoint
    void volumeToggleMute(const QString& source);
    void volumeApply(int percent,const QString& source);   // set, read back, log, show the HUD
    void pollTriggerSeek(float down,float up);             // L2/R2 seek in the player, called from pollController
    std::unique_ptr<QAbstractNativeEventFilter> remoteKeys_; // MediaKeyFilter (media_keys.h)
    RemoteKey remoteDeclined_=RemoteKey::None;               // the last native remote message declined, and when:
    qint64 remoteDeclinedAt_=-1000;                          // Qt's key-event echo of it is passed on unjudged
    bool triangleAction(const QString& source);            // the pad's Triangle, per surface; false = nothing took it
    void drawVolumeHud(QPainter& painter,double opacity);
    // ---- Media panels, surfaces A and B (audit/MEDIA-PANELS-FIRMWARE.md) ------------------------------
    // The firmware's player chrome, drawn from the containers' own objects. Every screen rect below is a
    // FIT and is labelled as one in the code: the paf projection for these pages is a runtime value that
    // no local container holds (audit 9.1, 9.2, 9.4), so only the object sizes, the texture sizes and the
    // cell units are firmware facts.
    // Surface A, the time bar: videoplayer_display_time_bar's Progress stack drawn with the container's
    // progression pair tex_progress_bar / tex_progress_bar_in (30x20) plus the tex_progress_bar_jump_indi
    // marker (20x12), and the 16x16 time-digit cells with the digits from MediaControls::formatTime.
    void drawVideoTimeBar(QPainter& painter,qint64 position,qint64 duration,double opacity=1.);
    void drawTimeCells(QPainter& painter,const QString& value,qreal edge,qreal top,bool rightAligned,double opacity=1.);
    // Surface A, the direct-key hint: the eight tex_cp_help_* 86x30 icons paired with the container's own
    // msg_*_with_icon strings, in place of the advice sentence this build wrote out in ASCII.
    void drawVideoHelpStrip(QPainter& painter);
    // Surface B2, the XMB music control panel (explore_plugin_full control_panel_page): tex_cp_panel 72x72
    // scaled 1.3 (0x270a54), the 10 status planes, progress_bar_playing_time 200x6 with label_playing_time,
    // and the 7-button group at 90x60 over the shared tex_cp_focus. Triangle opens it (audit 7.2); R keeps
    // the Repeat binding. Fade in 150 ms with 0 delay; the traced 150/100 base move is the 1.0->1.3 scale.
    void drawMusicPanel(QPainter& painter,double opacity);
    // Surface B1, the music player's own full-screen page (audit/MUSIC-PLAYER.md): audioplayer_plugin's
    // title_information_page (cover, track number, title, artist), play_progress_page (track number, time
    // bar, codec label), play_status_page (the status planes) and the control-panel hint, laid out by FITS
    // because the container carries no positions. After musicIdleMs without input the chrome fades and
    // the back_screen_plane darkens the page (INFERRED).
    void drawMusicPage(QPainter& painter);
    static constexpr double musicIdleMs=8000.;       // INFERRED: no idle timer is traced in audioplayer_plugin
    static constexpr double musicIdleFadeMs=600.;    // INFERRED
    static constexpr double musicIdleDim=.55;        // INFERRED: alpha of the black back_screen_plane when idle
    PafEase musicIdleEase_;
    // Visual Player stand-in (audit/MUSIC-PLAYER.md "Visualizer"): spectrum bars and a cover glow driven by the
    // PCM tap (musicvis.h). The smoothed state lives here; the analysis is on the tap's worker thread.
    void drawMusicVisualizer(QPainter& painter,double idle);
    std::array<float,40> musicBars_{},musicPeaks_{};
    float musicLevel_=0.f,musicVisAlpha_=0.f;
    bool musicVisualizerOn_=false;   // the Visual Player toggle: per playback session, off by default, reset when playback stops
    qint64 musicVisAt_=0;
    struct MusicVisStats {qint64 since=0;int frames=0;double drawMs=0,worstMs=0;} musicVisStats_;
    QString musicStatusKey_;                         // track + pause state the status plane last changed on
    qint64 musicStatusSince_=0;
    bool mediaAudioOnly() const;          // a local audio item is playing: no video track, not a photo
    void toggleMusicPanel();
    void closeMusicPanel();
    void musicPanelMove(int delta);
    void musicPanelActivate();
    static constexpr int musicPanelButtons=8;        // 7 explore_plugin_full cells + the Visual Player button (audioplayer_plugin art)
    static constexpr double musicPanelFadeMs=150.;    // FD: 0x270a00 (fade) / 0x270a04 (delay 0)
    static constexpr double musicPanelScale=1.3;      // FD: 0x270a54
    bool musicPanelOpen_=false;
    int musicPanelFocus_=0;
    PafEase musicPanelEase_;
    // The shared label path for the three media panels: the same GL glyph atlas as the XMB, at the size the
    // call site fits (RCO size=10 has no established pixel equivalent, audit 9.5).
    void mediaPanelText(QPainter& painter,const QRectF& rect,const QString& value,int size,const QColor& colour,int flags);
    void zoomPhoto(int direction);
    void resetPhotoView();
    void panPhoto(int dx,int dy);
    // GPU UI layer: 2x-sized 4x MSAA target, its resolve, and the premultiplied composite pass.
    std::unique_ptr<QOpenGLFramebufferObject> uiMsaa_,uiResolved_;
    QOpenGLShaderProgram uiComposite_;
    QString uiSignature_; // what the layer currently shows; redrawn when it changes
    TextRenderer textRenderer_;
    int uiRevision_=0;    // bumped on every shell change
    QImage wallpaperImage_,monthImage_;
    QHash<int,QVector3D> monthIconTints_;
    QString wallpaperPath_;
    quint64 wallpaperRequest_=0;
    std::unique_ptr<QOpenGLTexture> monthTexture_;
    void loadWallpaper();
    struct IconMaterial { QImage image; QRect bounds; };
    QHash<QString,IconMaterial> iconMaterials_;
    QHash<QString,QRect> imageBounds_; // visible (alpha) bounds per RCO image
    quint16 buttons_=0;
    qint64 repeatAt_=0;
    // Arrow keys held / pressed since the last poll, as D-pad bits: they repeat on the controller's firmware timing.
    quint16 keyHeld_=0,keyPressed_=0;
    void pollController();
    void renderBackground(float seconds,float light,int pixelWidth,int pixelHeight);
    // Original firmware background: qgl/lines back_colours0.fpo with this/next month's day and night palettes.
    struct BackgroundTime { float day=0,month=0,night=0,nightBrightness=0,nightDayBlend=1,alpha=1; };
    static constexpr double kNightBlend=.499947;
    static constexpr double kDaySpread=2.68038; // BACKGROUND.mnu DAYSPREAD, override/day (= yoake, higure)
    static double dayCurve(double t,double k);
    static BackgroundTime backgroundTime(const QDateTime& when);
    void initOriginalBackground();
    bool loadMonthTextures(int month);
    int texturesMonth_=0;
    void renderOriginalBackground(const BackgroundTime& time,int pixelWidth,int pixelHeight);
    QOpenGLShaderProgram original_;
    bool originalReady_=false;
    CgProgram backColours_;
    std::array<std::unique_ptr<QOpenGLTexture>,4> monthTextures_; // tex0..3: day this/next month, night this/next month
    Ribbon ribbon_;
    Hdr hdr_;
    Particles particles_;
    Coldboot coldboot_;
    bool introEnabled_=true;
    // Boot state of the background (RPCS3 capture of back_colours0 through a cold boot): _Alpha fades in as smoothstep
    // over 7.5 s from BootBG2; NIGHT WHIT BIAS goes from coldboot1 to coldboot2 on the boot weights.
    float backgroundAlpha_=1,backgroundToColdboot2_=1,backgroundToClock_=1;
    // The intro or the health notice after it is on screen (no UI); a key or button skips/dismisses (Coldboot::skip).
    bool introRunning() const { return coldboot_.holdsGui(); }
    IconRenderer icons_;
    QVector<IconRenderer::Icon> iconQueue_; // category icons for the firmware renderer, rebuilt with the UI layer
    std::vector<std::pair<quint64,QPointF>> windSites_; // icon centres for the particles' icon wind, rebuilt with the UI layer
    float presetBlend_=1;
    static constexpr double kColumnX=565; // focus column, 1080p px (firmware: camera 2700, eye x -5)
    // Firmware quad size (1080p px) of an icontex item icon in a column (RPCS3 capture): level 0 77.616, focused 184.8;
    // settings bullets inside a page from the page capture.
    static double itemIconSize(int level,bool bullet,double focus);
    bool forceOriginal_=false;
    QDateTime backgroundOverride_;                                    // fixed clock for measurement, invalid = now
    double secondsOverride_=-1;                                       // fixed animation time for A/B captures, -1 = now
    void drawScene(QPainter& painter);
    void drawPsMenu(QPainter& painter);
    void drawControllerHud(QPainter& painter);
    // ---- Message surfaces (dialogs.cpp) -------------------------------------------------------------
    // Shell decides which of the firmware's five surfaces a message is (MessageKind); these paths draw
    // the measured layouts. There is no dialog RCO - the box, its plate and its button row are built by
    // vsh.elf at run time (audit/DIALOGS-FIRMWARE.md section 1) - so every rectangle here is either a
    // measured reference value or a stated native fit, never a claim of a paf projection.
    void drawMessageSurface(QPainter& painter);      // dispatches on Shell::messageKind()
    void drawInlineMessage(QPainter& painter);       // D3 inline empty-state line
    void drawMessagePage(QPainter& painter);         // D4 titled status page
    void drawMessageBox(QPainter& painter);          // D1 modal box: plate, two bars, two hints
    void drawErrorMessagePage(QPainter& painter);    // D2 error-code page
    void drawMessageInfoPage(QPainter& painter);     // D5 information page
    // The Info family's second shape (SETTINGS-FIRMWARE.md 12.2): the depth-2 read-only table. It is a
    // variant of Info rather than a new MessageKind because the kind enum lives in Shell; the draw path
    // is chosen from the row that raised the message (Action::Information with a host:* payload), so the
    // D5 page keeps every other Information message unchanged. Measured: rules 199/879, rows on the
    // 40 px pitch from ink 291, labels right at 939, values left at 979, Back hint ink 897, no clock.
    void drawMessageTablePage(QPainter& painter);    // depth-2 read-only table page
    // The console's busy/wait page (page_busy, DIALOGS-FIRMWARE 2.8; FIDELITY-BEHAVIOUR N12/X5). It is not
    // a MessageKind: the shell raises it with one flag (Shell::busy(), its status text and the operation's
    // progress), so the scene draws it beside the message surface. Measured on the Internet Connection Test
    // wait burst (error/ictest2-burst, full frame, 57.9 fps): the D4 chrome - header on the message page's
    // boxes, the rule pair y199/y879 at 0.693 - one centred status line (ink 529..547), the Back hint at
    // 1155..1240 x 897..916, the page behind it dimmed and the info bar kept. progress < 0 (the default)
    // draws that measured indeterminate page; 0..1 adds the RCO-derived progress row (a stated fit - no
    // console frame of a progress page exists). spinnerPhase < 0 (the default) draws no D8 ring: the
    // measured page has none and page_busy's own rect is not recoverable from the RCO.
    void drawBusySurface(QPainter& painter,double progress=-1.,double spinnerPhase=-1.);
    // True while the busy page owns the screen: Shell::busy() with no message up and no media player
    // (those draw their own chrome). It is the class view.cpp's page wash and info-bar gate must include,
    // and drawPageChrome suppresses the page's own clock while it is true (the bar's face is the clock).
    bool busyPageUp() const;
    // The two modal message-page classes the 2026-10-28 frames measure (DIALOGS-FIRMWARE 13.7): the D4
    // titled page (MessageKind::Page) and the D2 error-code page (ErrorCode). They are the only surfaces
    // that sit on the console's dimmed page - page-network-test-error and page-network-connection-settings
    // read 47.0-51.0 where the same patch on a message-free frame reads 73-88, a whole-page least-squares
    // gain of 0.735 - and they are the only message surfaces that keep the whole info bar (the plate's
    // nine-slice ramps at y75/y132, the clock text and the filled clock face). The confirm box and the
    // depth-2 table page measure neither (the box's top band reads 73.6 against the root's 73.8 and its
    // top-right is flat at 76.9; the table page's is 77.0), so they return false and keep what they had.
    // drawPageChrome asks this before drawing the page's own clock: when the bar is present, the bar's
    // clock is the only clock on the frame.
    bool modalMessagePage() const;
    // The D4 chrome: icon + left title above rule 1 at the measured ink box (icon 109..149 x 154..189,
    // title ink 165 / 167), the page class's two flat rules - y and alpha are the caller's, because the
    // modal message page, the confirm box and the D5 information page each measure a pair (199/879 at
    // 177/255 on all three 1080p frames; the D5 page's own frame is information-page-01.png) while the
    // depth-2 table page carries its own 199/879 at 92 - and the analog clock face these pages keep.
    void drawPageChrome(QPainter& painter,const QString& title,double ruleTop,int ruleTopAlpha,double ruleBottom,int ruleBottomAlpha);
    void drawPageClock(QPainter& painter);
    // One button hint: a page_btnnavi glyph (30x30 texture, alpha-bounds fitted) and a system_plugin
    // label, laid out from the measured anchors: the glyph's ink box is 20 px at x 1155 with its top at
    // 897, the label's ink starts at x 1192 with its cap rows at 898 (a 21 px face: 2 px left bearing,
    // VCenter rect 4 px above the glyph's ink top). The box passes its own ink tops.
    struct Hint { QString glyph,label; };
    void drawHintRow(QPainter& painter,const QVector<Hint>& hints,double inkTop,double opacity);
    void drawMessageText(QPainter& painter,const QRectF& rect,const QString& value,int size,const QColor& colour,int flags=Qt::AlignLeft|Qt::AlignVCenter);
    // Wraps a body to a column with the same metric authority the glyphs use (blank paragraphs come back
    // as empty strings), so every line advances by the measured pitch instead of the layout engine's
    // leading. drawMessageLines returns the y below the last line.
    QStringList wrapMessage(const QString& body,double width,int size);
    double drawMessageLines(QPainter& painter,const QString& body,double top,double pitch,int size,const QColor& colour,double left,double width);
    // The message on screen. Latched while a box's traced fades run out, so its exit still draws.
    struct MessageShown { QString text; MessageKind kind=MessageKind::None; QStringList actions; };
    MessageShown messageShown_;
    // The only traced dialog motion: 200 units on the dialog's own label group (TRACED 0x00341BFC with
    // DATA 0x0068C978), 1000 on the plate child (TRACED 0x00344094 with DATA 0x0068CA1C), 100 per button
    // group for the hint row (system_plugin anim_*_show/_hide, accel mode 0), and 150 for the error
    // page's body settle (TRACED 0x00340964 with DATA 0x0068C2FC).
    PafEase dialogEase_,dialogPlateEase_,dialogHintEase_,dialogSettleEase_;
    static constexpr double dialogMilliseconds=200;
    static constexpr double dialogPlateMilliseconds=1000;
    static constexpr double dialogHintMilliseconds=100;
    static constexpr double dialogSettleMilliseconds=150;
    void grabIntroFrame();
    // Photo Gallery. gallery_ is non-null only while it is open; the key and pad handlers route to it first.
    std::unique_ptr<GalleryState> gallery_;
    QStringList galleryRoots_;
    bool galleryDrawing() const;
    bool galleryOwnsInput() const;
    void galleryPress(int key);
    void galleryOnIndexed(quint64 request,const GalleryScan& scan);
    void galleryResult(const GalleryController::Result& result);
    void paintGallery(double dt,double seconds);
    void drawGalleryChrome(QPainter& p);
    QString galleryText(const char* id,const QString& fallback);
    // PS3 on-screen keyboard, resolved from the firmware (audit/OSK-FIRMWARE.md): a 10x6 grid of 72x54 cells
    // from `oskfullkeypanel_plugin.rco` plus the decrypted module's row strings, a 7x7 symbol palette and a
    // prediction column. oskLayout_ is loaded once in the constructor and every key, rect and caption comes
    // from it; nothing here holds a layout table.
    // `masked` is the password composition mode (audit/JELLYFIN-NATIVE.md): the field draws one bullet per
    // character and the UI signature carries the bullet count instead of the text, so a secret cannot leak into
    // the frame, the signature cache or a capture. Every other caller leaves it false.
    struct Osk {
        bool open=false,masked=false;
        bool preserveWhitespace=false,allowEmpty=false;
        // The firmware's own state: `page` indexes OskLayout::pages (0 lower, 1 caps); the palette is the
        // `@#:` popup with its own 5-visible-row window over the 32 symbols; caretLit is the blink phase.
        bool paletteOpen=false,caretLit=true;
        int page=0,paletteRow=0,paletteColumn=0,paletteScroll=0;
        QString title,text;
        int row=0,column=0,cursor=0,maxLength=32;
        std::function<void(QString)> done;
    };
    Osk osk_;
    // The one RCO reader and the one resolved layout: both are startup work, never per-frame work.
    RcoObjects oskObjects_{assets_};
    OskLayout oskLayout_;
    void openOsk(const QString& title,const QString& initial,std::function<void(QString)> done,bool masked=false);
    void drawOsk(QPainter& p);
    void oskMove(int dx,int dy);
    void oskPress();                 // the focused key
    void oskPaletteMove(int dx,int dy);
    void oskPalettePress();
    void oskInsert(const QString& value);
    void oskBackspace();
    void oskClose(bool accept);
    QString oskCharacter(int row,int column) const;
    void drawImage(QPainter& painter,const QString& container,const QString& texture,const QRectF& rect,qreal opacity=1);
    // RCO item glyphs are the firmware's white glass masks (the folder family, album placeholders and the other
    // item textures): their alpha never reaches opaque, and the icon program shows only about a fifth of the mask
    // as white over the frame while keeping a bright rim where the mask ends. Prepared once per texture; opaque
    // RCO artwork (the store bag, ICON0 stills) comes back unchanged.
    QHash<QString,QImage> itemGlyphs_;
    const QImage& itemGlyph(const QString& container,const QString& texture);
    void drawItemGlyph(QPainter& painter,const QString& container,const QString& texture,const QRectF& rect,qreal opacity);
    void drawCategory(QPainter& painter,const Category& category,const QRectF& rect);
};
}
