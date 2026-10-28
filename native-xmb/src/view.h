#pragma once
#include "assets.h"
#include "coldboot.h"
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

namespace xmb {
class View final:public QOpenGLWidget,protected QOpenGLFunctions_3_3_Core {
public:
    View(Shell& shell,Assets& assets,QWidget* parent=nullptr);
    ~View() override;
    static double dayLight(double hour);
    // The XMB's moves are paf animations: 200 ms in accel mode 5 (vsh.elf PhXmBar focus moves FUN_003ad3bc /
    // FUN_003ad728; paf animation setup FUN_003981ec and evaluator FUN_00398748). Mode 5 closes a fixed fraction
    // k of the remaining distance every 60 Hz frame, k from a table by the duration in frames, and lands on the
    // target when the frame-snapped duration ends. A new target restarts the curve from the current value.
    // Mode 4 is the same per-frame form with its own k table; the page (depth) move uses it (RPCS3 capture).
    struct PafEase {
        double value=0;
        void retarget(double target,double milliseconds=200,int mode=5);
        void advance(double seconds);
        bool running() const { return running_; }
    private:
        double from_=0,to_=0,progress_=1,elapsed_=0,duration_=0,k_=0,growth_=1.000001;
        bool started_=false,running_=false;
    };
    static constexpr double moveMilliseconds=200;
    static constexpr double pageMilliseconds=300;
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
    // Verification taps dispatch synchronously, independent of foreground focus and the physical input poll.
    void replayKeyTap(int key);
    // Verification taps for the PS button (no physical controller in scripted runs). They drive the
    // same global watcher state machine a physical press does, including the 1 s hold.
    void replayPsTap();
    void replayPsHold();
    // The couch volume path, reachable from --script so a capture can pin an exact level. These are the
    // same functions the pad's L2/R2, the media keys and the native WM_APPCOMMAND filter call.
    void setMasterVolumePercent(int percent);   // VOLSET=<0..100>
    void restoreMasterVolume();                 // VOLRESTORE: the level and mute state read at start-up
protected:
    void initializeGL() override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    Shell& shell_;
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
    QVector<FrameSample> frameTrace_;
    void saveFrameTrace();
    qint64 previousFrameNs_=0,reportAt_=0;
    // PS button: the global watcher (independent of window focus) and the short controller-indicator
    // window a tap opens.
    PsWatcher psWatcher_;
    qint64 psHudUntil_=0;
    bool psPressActionable_=false;
    bool titleRunning() const;
    bool xmbOwnsForeground() const;
    void psPressed();
    void psHeldEvent();
    void psTappedEvent(qint64 durationMs);
    void psReleasedEvent(qint64 durationMs);
    void bringXmbForward(const QString& reason);
    void reassertForeground(const QString& reason);
    void activatePsMenuSelection();
    quint64 frames_=0;
    double categoryPosition_=6;
    int previousCategory_=6;
    struct Column { QVector<Entry> entries; int selection=0; double position=0; PafEase ease; };
    PafEase categoryEase_,depthEase_,fadeEase_;
    // Item-level Triangle options panel: its own open amount (slide-in) and focus position, animated with the
    // same paf curve as the settings panel. Drawn only while Shell::optionsOpen().
    PafEase optionsPanelEase_,optionsOpenEase_;
    bool optionsWasOpen_=false;
    // The console's notification pop-up (Shell's queue). The ease is its presence on screen: it slides and
    // fades in over 200 ms (paf mode 5, the XMB's own move curve) and back out the same way. The last content
    // is kept while it leaves, because Shell has already dropped the entry by then.
    PafEase notificationEase_;
    struct NotificationShown { QString title,detail,icon; };
    NotificationShown notificationShown_;
    bool notificationTextureWarned_=false;
    void drawNotification(QPainter& painter,double amount);
    // The active sidecar subtitle cue (Shell::subtitleCueText): centred in the lower third, above the
    // media controls, drawn through the GL glyph atlas with a dark outline so it stays legible over a
    // bright frame. audit/SUBTITLES.md.
    void drawSubtitle(QPainter& painter,const QString& cue);
    // A 64x64 RCO tile (the notification plaque) stretched to any size without turning its rounded corners
    // into ellipses: the corners keep their pixels, the edges and centre stretch.
    void drawNineSlice(QPainter& painter,const QString& container,const QString& texture,const QRectF& rect,qreal inset,qreal opacity);
    QVector<Column> columns_;
    QVector<Column> outgoing_;
    int outgoingCategory_=6;
    double categoryFade_=0;
    // Time since the focused row (category, depth, selection) last changed: label glow phase and idle label fade.
    QElapsedTimer focusClock_;
    qint64 focusTimeOverride_=-1;
    qint64 focusTime() const { return focusTimeOverride_>=0?focusTimeOverride_:(focusClock_.isValid()?focusClock_.elapsed():0); }
    qint64 focusKey_=-1;
    double depthPosition_=0;
    int previousDepth_=0;
    void syncColumns();
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
    int volumeRamp_=0;               // the held trigger: -1 L2, +1 R2, 0 released
    qint64 volumeRampAt_=0;          // next held-trigger step deadline
    qint64 volumeHudAt_=0;           // when the HUD's 3 s interval started, for the opt-in fade trace
    bool volumeHudVisible_=false;    // for the opt-in trace of the HUD's own expiry
    bool volumeWarned_=false;        // one line only when the endpoint cannot be read
    std::unique_ptr<QAbstractNativeEventFilter> volumeKeys_;
    bool volumeInputBlocked() const;                       // a surface that owns input or the frame is up
    bool volumeKeyAction(int action,const char* source);   // 1 up, 2 down, 3 mute; false = key not taken
    void volumeStep(int direction,const QString& source);  // one 5% step through the host endpoint
    void volumeToggleMute(const QString& source);
    void volumeApply(int percent,const QString& source);   // set, read back, log, show the HUD
    void pollVolumeRamp(float down,float up,qint64 now);   // held L2/R2 repeat, called from pollController
    void drawVolumeHud(QPainter& painter,double opacity);
    // ---- Media panels, surfaces A and B (audit/MEDIA-PANELS-FIRMWARE.md) ------------------------------
    // The firmware's player chrome, drawn from the containers' own objects. Every screen rect below is a
    // FIT and is labelled as one in the code: the paf projection for these pages is a runtime value that
    // no local container holds (audit 9.1, 9.2, 9.4), so only the object sizes, the texture sizes and the
    // cell units are firmware facts.
    // Surface A, the time bar: videoplayer_display_time_bar's Progress stack drawn with the container's
    // progression pair tex_progress_bar / tex_progress_bar_in (30x20) plus the tex_progress_bar_jump_indi
    // marker (20x12), and the 16x16 time-digit cells with the digits from MediaControls::formatTime.
    void drawVideoTimeBar(QPainter& painter,qint64 position,qint64 duration);
    void drawTimeCells(QPainter& painter,const QString& value,qreal edge,qreal top,bool rightAligned);
    // Surface A, the direct-key hint: the eight tex_cp_help_* 86x30 icons paired with the container's own
    // msg_*_with_icon strings, in place of the advice sentence this build wrote out in ASCII.
    void drawVideoHelpStrip(QPainter& painter);
    // Surface B2, the XMB music control panel (explore_plugin_full control_panel_page): tex_cp_panel 72x72
    // scaled 1.3 (0x270a54), the 10 status planes, progress_bar_playing_time 200x6 with label_playing_time,
    // and the 7-button group at 90x60 over the shared tex_cp_focus. Triangle opens it (audit 7.2); R keeps
    // the Repeat binding. Fade in 150 ms with 0 delay; the traced 150/100 base move is the 1.0->1.3 scale.
    void drawMusicPanel(QPainter& painter,double opacity);
    bool mediaAudioOnly() const;          // a local audio item is playing: no video track, not a photo
    void toggleMusicPanel();
    void closeMusicPanel();
    void musicPanelMove(int delta);
    void musicPanelActivate();
    static constexpr int musicPanelButtons=7;
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
    // The intro is on screen (logo, no UI); any key or button skips it.
    bool introRunning() const { return coldboot_.ready() && coldboot_.seconds()<Coldboot::showGui; }
    IconRenderer icons_;
    QVector<IconRenderer::Icon> iconQueue_; // category icons for the firmware renderer, rebuilt with the UI layer
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
    // The D4 chrome: icon + left title above rule 1 at the measured ink y 154..188, the two flat rules
    // (alpha ~0.22 at y 197, ~0.15 at y 877, uniform across x - not tex_msgdialog_bar, which is
    // centre-bright), and the analog clock face these pages keep while the info bar's plate is hidden.
    void drawPageChrome(QPainter& painter,const QString& title);
    void drawPageClock(QPainter& painter);
    // One button hint: a page_btnnavi glyph (30x30 texture, alpha-bounds fitted) and a system_plugin
    // label, laid out from the measured x 1155. The page kinds put its ink at y 889..909 (measured); a
    // modal box has no measured hint baseline and passes its own, just below the plate.
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
