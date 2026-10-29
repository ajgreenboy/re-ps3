#include "view.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMainWindow>
#include <QSurfaceFormat>
#include <QKeyEvent>
#include <memory>
#include <QSettings>
#include "catalog.h"
#include "rsx_program.h"
#include "p3t.h"
#include "rco_objects.h"
#include "content_registry.h"
#include "steam_library.h"
#include "steam_downloads.h"
#include "steam_accounts.h"
#include "steam_store.h"
#include "steam_owned.h"
#include "steam_collections.h"
#include "steam_overrides.h"
#include "steam_family.h"
#include "host.h"
#include "pad.h"
#include "settings_menu.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QMap>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPainter>
#include <QImageReader>
#include <QTimer>
#include <QElapsedTimer>
#include <QScreen>
#include <QtEndian>
#include <QTextLayout>
#include <QRawFont>
#include <QGlyphRun>
#include <cstdio>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#endif

#ifdef Q_OS_WIN
// Crash diagnosis: an unhandled exception writes its code and a symbolized stack (re-ps3-native.pdb next to the exe)
// to XMB_LOG, or crash.log beside the executable.
static LONG WINAPI writeCrashStack(EXCEPTION_POINTERS* info) {
    const QString path=qEnvironmentVariableIsSet("XMB_LOG")?qEnvironmentVariable("XMB_LOG")
        :QCoreApplication::applicationDirPath()+"/crash.log";
    FILE* out=_wfopen(reinterpret_cast<const wchar_t*>(path.utf16()),L"a");
    if(!out)return EXCEPTION_CONTINUE_SEARCH;
    const HANDLE process=GetCurrentProcess(),thread=GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME);SymInitialize(process,nullptr,TRUE);
    std::fprintf(out,"crash: exception 0x%08lx at %p\n",info->ExceptionRecord->ExceptionCode,info->ExceptionRecord->ExceptionAddress);
    CONTEXT context=*info->ContextRecord;STACKFRAME64 frame{};
    frame.AddrPC.Offset=context.Rip;frame.AddrFrame.Offset=context.Rbp;frame.AddrStack.Offset=context.Rsp;
    frame.AddrPC.Mode=frame.AddrFrame.Mode=frame.AddrStack.Mode=AddrModeFlat;
    for(int depth=0;depth<40 && StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,
                                            SymFunctionTableAccess64,SymGetModuleBase64,nullptr);++depth) {
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO)+256]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=255;DWORD64 displacement=0;
        IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD lineDisplacement=0;
        const bool named=SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol);
        const bool lined=SymGetLineFromAddr64(process,frame.AddrPC.Offset,&lineDisplacement,&line);
        std::fprintf(out,"  #%02d %p %s%s%s:%lu\n",depth,reinterpret_cast<void*>(frame.AddrPC.Offset),named?symbol->Name:"?",
                     lined?" ":"",lined?line.FileName:"",lined?line.LineNumber:0ul);
    }
    std::fclose(out);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

namespace {
// XMB_INPUT_TRACE=1 logs each input decision (audit/PS-BUTTON.md): source=script|pad|keyboard, the action, and
// whether the shell accepted it. Script decisions are logged by the token loop below and pad decisions by
// pad.cpp; this filter logs a navigation key when the shell dispatches it. Keyboard input is never gated - the
// script drives the shell through the view's own key handling, so a real keyboard keeps working in a scripted
// run and only the live pad is withheld.
bool inputTraceEnabled() {
    static const bool on=[]{
        const QString value=qEnvironmentVariable("XMB_INPUT_TRACE");
        return !value.isEmpty() && value!="0";
    }();
    return on;
}
QString keyboardAction(int key) {
    switch(key) {
    case Qt::Key_Left:return QStringLiteral("LEFT");
    case Qt::Key_Right:return QStringLiteral("RIGHT");
    case Qt::Key_Up:return QStringLiteral("UP");
    case Qt::Key_Down:return QStringLiteral("DOWN");
    case Qt::Key_Return:case Qt::Key_Enter:return QStringLiteral("ENTER");
    case Qt::Key_Escape:return QStringLiteral("ESC");
    case Qt::Key_Backspace:return QStringLiteral("BACKSPACE");
    case Qt::Key_Space:return QStringLiteral("SPACE");
    case Qt::Key_PageUp:return QStringLiteral("PGUP");
    case Qt::Key_PageDown:return QStringLiteral("PGDN");
    case Qt::Key_BracketLeft:return QStringLiteral("PREV");
    case Qt::Key_BracketRight:return QStringLiteral("NEXT");
    case Qt::Key_M:return QStringLiteral("SHUFFLE");
    case Qt::Key_R:return QStringLiteral("REPEAT");
    case Qt::Key_T:return QStringLiteral("TRIANGLE");
    case Qt::Key_F5:return QStringLiteral("REFRESH");
    case Qt::Key_F10:return QStringLiteral("F10");
    case Qt::Key_F11:return QStringLiteral("F11");
    case Qt::Key_Plus:return QStringLiteral("ZOOMIN");
    case Qt::Key_Minus:return QStringLiteral("ZOOMOUT");
    case Qt::Key_0:return QStringLiteral("RESETVIEW");
    case Qt::Key_Q:return QStringLiteral("ROTATELEFT");
    case Qt::Key_E:return QStringLiteral("ROTATERIGHT");
    default:return {};
    }
}
class KeyboardTrace final : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject*,QEvent* event) override {
        if(event->type()!=QEvent::KeyPress)return false;
        const auto* key=static_cast<QKeyEvent*>(event);
        if(key->isAutoRepeat())return false;   // one line per press, not one per repeat
        const QString action=keyboardAction(key->key());
        if(!action.isEmpty())qInfo().noquote()<<QString("input: trace source=keyboard action=%1 accepted=1").arg(action);
        return false;
    }
};
// ---- --image-audit: the container resolution table for the ICON-GAP-AUDIT image inventory ----------------
// One row per firmware image named in audit/ICON-GAP-AUDIT.md section 4 (bucket B) and section 5 (bucket C),
// with the container the drawing site (or the audit row) asks for. "*" as the container expands to every
// vsh/resource RCO with that prefix; "*" as the name list expands to every named image in that container.
// Rows whose audit entry has no firmware image (B15, B16, C5, C10) are printed as notes and not counted.
// Names are the firmware's exact image-table labels; where the audit abbreviated or mis-filed one - C14's
// "tex_default_scroll_arrow_*", C26's explore_category_video:tex_3D, C27's explore_category_music:tex_track_*,
// C33's 17-icon count, C41's container count - the table reports the real owner instead of hiding it.
struct IconAuditRow { const char* bucket; const char* container; const char* names; };
const IconAuditRow kIconAuditRows[] = {
    {"B1","qgl/icontex","1n"},
    {"B2","qgl/icontex","35n"},
    {"B3","system_plugin","tex_msgdialog_bg tex_msgdialog_bg2 tex_msgdialog_bg_deep"},
    {"B4","system_plugin","tex_msgdialog_bar"},
    {"B5","system_plugin","tex_optionmenu_bg"},
    {"B6","system_plugin","tex_optionmenu_line tex_optionmenu_base tex_optionmenu_game"},
    {"B7","impose_plugin","tex_controller_number tex_red"},
    {"B8","explore_category_friend","tex_indi_AFK tex_indi_Game tex_indi_Me tex_indi_Message tex_indi_NewRoom tex_indi_None tex_indi_Sign_in tex_indi_Sign_out"},
    {"B9","oskfullkeypanel_plugin","BUTTON_BASE_WHITE BUTTON_BASE_BLUE_GRAY BUTTON_BASE_B BUTTON_BASE_G BUTTON_BASE_O BUTTON_BASE_P BUTTON_BASE_E BUTTON_BASE_SHORT BUTTON_FORCUS"},
    {"B10","oskfullkeypanel_plugin","LABEL_UP LABEL_DOWN LABEL_LEFT LABEL_RIGHT"},
    {"B11","oskfullkeypanel_plugin","EN_LABEL_DELETE EN_LABEL_SPACE EN_LABEL_RETURN EN_LABEL_MODE EN_LABEL_CLOSE MODE_GUIDE_ALPHABET MODE_GUIDE_ALPHABET_CAP LIST_WINDOW_FOCUS"},
    {"B12","system_plugin","tex_default_text_field tex_default_text_field_focus"},
    {"B13","explore_plugin_full","tex_video"},
    {"B14","explore_plugin_full","tex_photo"},
    {"C1","system_plugin","tex_controller"},
    {"C2","xmb_plugin_normal","tex_indi_message"},
    {"C3","xmb_plugin_normal","tex_indi_chat"},
    {"C4","xmb_plugin_normal","tex_indi_plus"},
    {"C6","system_plugin","tex_battery_indicator"},
    {"C7","impose_plugin","tex_battery tex_battery_shadow tex_charge_arrow tex_charge_arrow_shadow tex_error_batsu"},
    {"C8","system_plugin","tex_rectangle"},
    {"C9","system_plugin","tex_triangle"},
    {"C11","system_plugin","tex_busy tex_busy_s tex_busy_l tex_busy_shadow"},
    {"C12","explore_plugin_full","tex_cp_stop tex_cp_prev tex_cp_next tex_searchb_status tex_searchf_status tex_stop_status tex_prev_status tex_next_status"},
    {"C13","system_plugin","tex_arrow_left"},
    {"C14","system_plugin","tex_default_scroll_base tex_default_scroll_base_shadow tex_default_scroll_slider tex_default_scroll_arrow_up tex_default_scroll_arrow_up_shadow tex_default_scroll_arrow_down tex_default_scroll_arrow_down_shadow"},
    {"C15","system_plugin","tex_default_check"},
    {"C16","system_plugin","tex_3x3_focus tex_3x3_shadow"},
    {"C17","system_plugin","tex_line tex_line_shadow"},
    {"C18","system_plugin","tex_common_dialog tex_common_dialog_shadow"},
    {"C19","system_plugin","tex_operation_guide_base tex_operation_guide_base_shadow"},
    {"C20","system_plugin","tex_pointer_arrow tex_pointer_click tex_pointer_finger tex_pointer_grab tex_pointer_hand tex_pointer_pen"},
    {"C21","system_plugin","tex_arrow_up tex_arrow_up_shadow tex_arrow_down tex_arrow_down_shadow tex_arrow_left tex_arrow_left_shadow tex_arrow_right tex_arrow_right_shadow"},
    {"C22","system_plugin","tex_default_spin_arrow_up tex_default_spin_arrow_up_shadow tex_default_spin_arrow_down tex_default_spin_arrow_down_shadow"},
    {"C23","system_plugin","tex_temporary_icon"},
    {"C25","explore_plugin_full","tex_indi_AFK tex_indi_Game tex_indi_Me tex_indi_Message tex_indi_NewRoom tex_indi_None tex_indi_Sign_in tex_indi_Sign_out"},
    {"C26","explore_category_video","tex_3D tex_divx tex_wmv tex_avc tex_mpg1 tex_mpg2 tex_mpg4 tex_mjpg"},
    {"C27","explore_category_music","tex_track_0 tex_track_1 tex_track_2 tex_track_3 tex_track_4 tex_track_5 tex_track_6 tex_track_7 tex_track_8 tex_track_9 tex_track_base"},
    {"C27","explore_plugin_full","tex_track_0 tex_track_1 tex_track_2 tex_track_3 tex_track_4 tex_track_5 tex_track_6 tex_track_7 tex_track_8 tex_track_9 tex_track_base"},
    {"C28","explore_category_photo","tex_3D tex_screenshot tex_multiangle tex_download_music tex_download_photo tex_download_video"},
    {"C29","explore_category_psn","trophy_tex_grade_platinum trophy_tex_grade_gold trophy_tex_grade_silver trophy_tex_grade_bronze trophy_tex_grade_hidden trophy_tex_default trophy_tex_capsule trophy_tex_addon"},
    {"C30","explore_category_network","tex_dl_dst_game tex_dl_dst_music tex_dl_dst_photo tex_dl_dst_video"},
    {"C31","sysconf_plugin","tex_cable_avmulti tex_cable_component tex_cable_composite tex_cable_d tex_cable_dsub tex_cable_hdmi tex_cable_pin_lr tex_cable_s tex_cable_scart tex_cable_spdif"},
    {"C32","sysconf_plugin","tex_logo_amg tex_logo_atok tex_logo_atrac tex_logo_divx tex_logo_dnas tex_logo_dnas_sd tex_logo_dolby_digital tex_logo_dolby_truehd tex_logo_dts tex_logo_ezitext tex_logo_flash tex_logo_java tex_logo_ps tex_logo_ps_sd tex_logo_qgames tex_logo_rsa"},
    {"C33","sysconf_plugin","tex_bar_bddvd_icon tex_bar_bddvd_shadow_icon tex_bar_console_icon tex_bar_console_shadow_icon tex_bar_device_icon tex_bar_device_shadow_icon tex_bar_display_icon tex_bar_display_shadow_icon tex_bar_game_icon tex_bar_game_shadow_icon tex_bar_init_icon tex_bar_init_shadow_icon tex_bar_music_icon tex_bar_music_shadow_icon tex_bar_network_icon tex_bar_network_shadow_icon tex_bar_power_icon tex_bar_power_shadow_icon tex_bar_remote_icon tex_bar_remote_shadow_icon tex_bar_security_icon tex_bar_security_shadow_icon tex_bar_sound_icon tex_bar_sound_shadow_icon tex_bar_theme_icon tex_bar_theme_shadow_icon tex_bar_time_icon tex_bar_time_shadow_icon"},
    {"C34","oskpanel_plugin","*"},
    {"C35","xmb_ingame","tex_user tex_photo tex_music tex_video tex_game tex_network tex_friend tex_sysconf tex_tv tex_psn"},
    {"C36","explore_plugin_full","tex_Sign_Up tex_Signing_In tex_ps_plus_invitation tex_psplus_icon"},
    {"C37","premo_plugin","tex_circle tex_cross tex_triangle tex_rectangle tex_circle_shadow tex_cross_shadow tex_triangle_shadow tex_rectangle_shadow tex_premo_bar"},
    {"C38","deviceconf_plugin","tex_battery tex_bt_state_base tex_bt_state_on tex_level_meter_blue tex_level_meter_grey tex_level_meter_red tex_level_meter_yellow"},
    {"C39","auth_plugin","tex_bar_security_icon tex_bar_security_shadow_icon"},
    {"C40","netconf_plugin","tex_infobar_icon tex_infobar_initial_setting_icon tex_network_connect_icon tex_arrow_left"},
    {"C41","*explore_category_","tex_aa_plane"},
};
int runImageAudit(xmb::Assets& assets,const QString& outputPath) {
    QFile file;
    if(!outputPath.isEmpty()) {
        file.setFileName(outputPath);
        if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){qCritical()<<file.errorString();return 2;}
    }
    const auto writeLine=[&](const QString& line){
        if(file.isOpen()){file.write(line.toUtf8()+'\n');return;}
        // A GUI-subsystem build has no console, so Qt's message handler goes to the debugger instead of a
        // redirected stdout: write the bytes ourselves so `--image-audit > table.txt` works headless.
        const QByteArray bytes=line.toUtf8()+'\n';
        std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);
        std::fflush(stdout);
    };
    const QStringList allow=assets.sameArtAllowList();
    const QStringList rules=assets.containerOverrideRules();
    int total=0,decoded=0,migrated=0,absent=0,wrongContainer=0,decodeFailed=0;
    QMap<QString,int> perContainer;
    writeLine(QStringLiteral("# image-audit: inventory = ICON-GAP-AUDIT.md bucket B + bucket C; each line is name container decoded(w,h) pixels"));
    writeLine(QStringLiteral("# image-audit: same-art allow-list = %1 (%2 names; bestImage's pixel gate applies to these only)").arg(allow.join(", ")).arg(allow.size()));
    writeLine(QStringLiteral("# image-audit: container-override rules = %1").arg(rules.size()));
    for(const auto& rule:rules)writeLine(QStringLiteral("#   %1").arg(rule));
    for(const auto& row:kIconAuditRows) {
        QStringList containers;
        const QString containerSpec=QString::fromLatin1(row.container);
        if(containerSpec.startsWith('*')) {
            const QString prefix=containerSpec.mid(1);
            for(const auto& entry:QDir(QDir(assets.root()).filePath("vsh/resource")).entryInfoList({"*.rco"},QDir::Files,QDir::Name))
                if(entry.completeBaseName().startsWith(prefix))containers<<entry.completeBaseName();
        } else containers<<containerSpec;
        for(const auto& container:containers) {
            QStringList names;
            const QString nameSpec=QString::fromLatin1(row.names);
            if(nameSpec=="*") {
                try { names=assets.imageNames(container); } catch(const std::exception&) { names.clear(); }
                names.sort();
            } else names=nameSpec.split(' ',Qt::SkipEmptyParts);
            for(const auto& name:names) {
                ++total;
                xmb::Assets::ImageHit hit;
                const QImage& image=assets.iconImage(container,name,&hit);
                const bool ok=!image.isNull();
                QString gate=QStringLiteral("n/a");
                if(allow.contains(name)) {
                    assets.bestImage(container,name);
                    const auto decision=assets.bestImageDecision(container,name);
                    if(decision.upgraded)
                        gate=QStringLiteral("pass->%1 %2x%3").arg(decision.container).arg(decision.width).arg(decision.height);
                    else if(decision.gateApplied)gate=QStringLiteral("reject(kept source)");
                    else if(!image.isNull() && std::max(image.width(),image.height())>=96)
                        gate=QStringLiteral("n/a(source>=96px)");
                    else gate=QStringLiteral("n/a(no larger copy)");
                }
                QString line;
                if(ok) {
                    ++decoded;
                    ++perContainer[hit.container];
                    if(hit.migrated)++migrated;
                    line=QStringLiteral("%1 %2 %3 %4x%5 %6 requested=%7 alias=%8 gate=%9")
                        .arg(QString::fromLatin1(row.bucket),name,hit.container)
                        .arg(image.width()).arg(image.height())
                        .arg(qint64(image.width())*image.height())
                        .arg(container,hit.migrated?QStringLiteral("yes"):QStringLiteral("no"),gate);
                } else {
                    const QStringList owners=assets.imageContainers(name);
                    QString why;
                    if(owners.isEmpty()){++absent;why=QStringLiteral("absent-from-all-122-RCOs");}
                    else if(owners.contains(container)){++decodeFailed;why=QStringLiteral("decode-failed");}
                    else {++wrongContainer;why=QStringLiteral("absent-from-")+container+QStringLiteral(";exists-in=")+owners.join(',');}
                    line=QStringLiteral("%1 %2 - - 0 requested=%3 alias=no gate=%4 reason=%5")
                        .arg(QString::fromLatin1(row.bucket)).arg(name).arg(container).arg(gate).arg(why);
                }
                writeLine(line);
            }
        }
    }
    QStringList ownerLines;
    for(auto it=perContainer.cbegin();it!=perContainer.cend();++it)
        ownerLines<<QStringLiteral("%1 %2").arg(it.key()).arg(it.value());
    writeLine(QStringLiteral("# image-audit: %1 names in %2 inventory rows: %3 decoded, %4 unresolved (%5 requested from a container that ships them elsewhere, %6 absent from every RCO, %7 decode failures); %8 migrated by a container-override rule")
        .arg(total).arg(int(sizeof(kIconAuditRows)/sizeof(kIconAuditRows[0]))).arg(decoded).arg(total-decoded)
        .arg(wrongContainer).arg(absent).arg(decodeFailed).arg(migrated));
    writeLine(QStringLiteral("# image-audit: owner containers: %1").arg(ownerLines.join(", ")));
    writeLine(QStringLiteral("# image-audit: note B15 - no firmware image: the analog clock face is drawn by vsh.elf (no RCO image object)"));
    writeLine(QStringLiteral("# image-audit: note B16 - no firmware image: the in-player current-item dot has no mbox_* object"));
    writeLine(QStringLiteral("# image-audit: note C5  - no firmware image: the avatar slot's image= is null (signed-in user data, not an RCO)"));
    writeLine(QStringLiteral("# image-audit: note C10 - not an RCO image: the selected theme's .p3t info/icon goes through P3t, not Assets"));
    writeLine(QStringLiteral("# image-audit: note C24 - same object as B3 (system_plugin:tex_msgdialog_bg_deep); counted once"));
    const auto probe=[&](const QString& container,const QString& name) {
        const QImage& source=assets.image(container,name);
        const QImage& chosen=assets.bestImage(container,name);
        const auto decision=assets.bestImageDecision(container,name);
        QString gate;
        if(decision.upgraded)
            gate=QStringLiteral("pass->%1 %2x%3").arg(decision.container).arg(decision.width).arg(decision.height);
        else if(decision.gateApplied)gate=QStringLiteral("reject(kept source)");
        else if(!source.isNull() && std::max(source.width(),source.height())>=96)gate=QStringLiteral("n/a(source>=96px)");
        else gate=QStringLiteral("n/a(no larger copy)");
        writeLine(QStringLiteral("# image-audit: allow-list probe %1/%2 source=%3 chosen=%4 gate=%5 (%6)")
            .arg(container,name,
                 source.isNull()?QStringLiteral("undecoded"):QStringLiteral("%1x%2").arg(source.width()).arg(source.height()),
                 chosen.isNull()?QStringLiteral("undecoded"):QStringLiteral("%1x%2").arg(chosen.width()).arg(chosen.height()),
                 gate,decision.reason));
    };
    probe(QStringLiteral("xmb_plugin_normal"),QStringLiteral("tex_psn"));
    probe(QStringLiteral("explore_plugin_full"),QStringLiteral("tex_video"));
    probe(QStringLiteral("explore_plugin_full"),QStringLiteral("tex_photo"));
    if(file.isOpen())file.close();
    return 0;
}
}

int main(int argc,char** argv) {
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(writeCrashStack);
#endif
    // The Windows build has no console: XMB_LOG=<file> appends every Qt message to that file (local diagnosis).
    if(qEnvironmentVariableIsSet("XMB_LOG"))qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message){
        QFile file(qEnvironmentVariable("XMB_LOG"));if(file.open(QIODevice::Append|QIODevice::Text))file.write((message+'\n').toUtf8());
    });
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setSwapInterval(1);format.setSamples(4);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);app.setApplicationName("re-ps3-native");app.setApplicationVersion("0.2.0");QImageReader::setAllocationLimit(128);
    QCommandLineParser parser;parser.setApplicationDescription("Native C++ XMB recreation");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"dev-flash","Local dev_flash asset directory","directory"});
    parser.addOption({"dev-hdd0","Local dev_hdd0 directory (default: next to dev_flash)","directory"});
    parser.addOption({"vsh-modules","Local directory of decrypted VSH modules (*.prx), for data tables the plugins carry","directory"});
    parser.addOption({"data-dir","Writable native application data directory","directory"});
    parser.addOption({"inventory","Write the local XMBML definition inventory and exit","json-file"});
    parser.addOption({"icon-sheet","Write a local contact sheet of original QGL normal maps and exit","image-file"});
    parser.addOption({"no-intro","Start at the XMB without the cold-boot intro (also implied by --script, --background-clock and --measure-reference)"});
    parser.addOption({"script","Replay key presses and save framebuffer grabs (local verification)","tokens"});
    parser.addOption({"background-clock","Fix background and icon-material time for reference captures (ISO date/time); UI clock stays live","date-time"});
    parser.addOption({"render-clock","Fix the background/ribbon/particles/icon animation time in seconds for deterministic A/B captures","seconds"});
    parser.addOption({"ui-clock","Fix the displayed UI date/time for local captures; never changes the host clock","date-time"});
    parser.addOption({"focus-time","Fix focused-label age in milliseconds for glow/idle-fade captures","milliseconds"});
    // ---- Session and power (Phase 4, audit/SESSION-POWER.md) ----
    parser.addOption({"kiosk","Appliance mode: frameless fullscreen on the chosen display, straight to the XMB (no cold-boot intro)"});
    parser.addOption({"display","Screen index for --kiosk (0 = first screen; default: the primary screen)","index"});
    parser.addOption({"start-windowed","Start in a window even when the stored preference is fullscreen (local testing)"});
    parser.addOption({"pad-report","Write the attached PlayStation controller's kind, connection and battery as JSON, then exit","json-file"});
    parser.addOption({"pad-check","Run documented synthetic DualSense/DualShock 4 input reports through the pad parser, write JSON, then exit","json-file"});
    parser.addOption({"motion-report","Write easing samples at different host frame rates without opening a window","json-file"});
    parser.addOption({"steam-root","Override the local Steam installation directory","directory"});
    parser.addOption({"steam-report","Write discovered installed Steam games and exit without launching them","json-file"});
    parser.addOption({"steam-friends-report","Write Steam friends or an explicit connection error without opening a window","json-file"});
    parser.addOption({"steam-store-report","Query the public Steam store catalogue and write the rows as JSON without opening a window","json-file"});
    parser.addOption({"steam-store-query","Free-text store search for --steam-store-report (default: top sellers)","term"});
    parser.addOption({"steam-family-report","Write the local Steam Families membership and any family-shared list, then exit","json-file"});
    parser.addOption({"steam-artwork-report","List per-game artwork overrides under <data>/artwork and exit","json-file"});
    parser.addOption({"steam-library-report","Write this account's Steam library organised by its own collections (names, install/launch handoffs) without opening a window","json-file"});
    parser.addOption({"steam-collections-report","Write this account's Steam library collections and their app ids without opening a window","json-file"});
    parser.addOption({"owned-games-report","Write the games this Steam account owns, with an install/launch handoff each, without opening a window","json-file"});
    parser.addOption({"steam-downloads-report","Write Steam's own download/update queue without opening a window","json-file"});
    parser.addOption({"steam-accounts-report","Write the remembered Steam accounts and the honest switch plan, without opening a window","json-file"});
    parser.addOption({"import-apps","Validate and merge a games/service-app manifest into the native library, then exit","json-file"});
    parser.addOption({"export-image","Export RCO images (container:name,...@directory) as local PNGs and exit","spec"});
    parser.addOption({"rco-text","Write the English strings of RCO containers (a,b@file) to a local text file and exit","spec"});
    parser.addOption({"rco-images","Write the image names of every vsh/resource RCO to a local text file and exit","text-file"});
    parser.addOption({"rco-tree","Write the RCO object model (objects, animations, typed properties) of named containers (<name>[,<name>...][@file]; * = every vsh/resource RCO) and exit","spec"});
    parser.addOption({"image-audit","Print the container resolution table for the ICON-GAP-AUDIT bucket B/C image inventory and exit ([file] instead of stdout is accepted as a positional argument)"});
    // ---- Settings (audit/SETTINGS-COVERAGE.md 5.1) ----
    parser.addOption({"settings-report","Write the whole settings surface (every page, every row, its live value, source and coverage) as JSON, then exit","json-file"});
    parser.addOption({"settings-page","Limit --settings-report's pages array to one page id (system, video, theme, ...)","page-id"});
    parser.addOption({"month-sheet","Write a local contact sheet of the qgl/lines month_bg day/night textures and exit","image-file"});
    parser.addOption({"font-report","Write which firmware font renders each sample string's glyphs and exit","text-file"});
    parser.addOption({"decode-fp","Decode RSX fragment-program captures (archive:lib/x.fpo@dir) into named parameter values and exit","spec"});
    parser.addOption({"dump-cg","Translate the original qgl/lines Cg programs to GLSL into a local folder and exit","directory"});
    parser.addOption({"measure-reference","Render the background offscreen, write statistics comparable to audit/VIDEO-REFERENCES.md and exit","text-file"});
    parser.addOption({"p3t-dump","List a PS3 theme's elements and write its decoded pictures and sounds to a local folder (theme.p3t@directory) and exit","spec"});parser.process(app);
    if(parser.isSet("p3t-dump")) { // local analysis only: theme-derived files, keep out of Git
        const auto spec=parser.value("p3t-dump").split('@');
        if(spec.size()!=2){qCritical()<<"use --p3t-dump theme.p3t@directory";return 2;}
        QString error;const auto theme=xmb::P3t::load(spec[0],&error);
        if(theme.isNull()){qCritical().noquote()<<error;return 2;}
        const QDir out(spec[1]);QDir().mkpath(out.path());
        QFile list(out.filePath("elements.txt"));
        if(!list.open(QIODevice::WriteOnly|QIODevice::Text)){qCritical()<<list.errorString();return 2;}
        QTextStream stream(&list);
        for(const auto& element:theme.elements()) {
            stream<<QString(element.depth*2,' ')<<element.name;
            auto keys=element.attributes.keys();keys.sort();
            for(const auto& key:keys) {
                const auto& a=element.attributes[key];using Kind=xmb::P3t::Attribute::Kind;
                stream<<" "<<key<<"="<<(a.kind==Kind::Int?QString::number(a.integer):a.kind==Kind::Float?QString::number(a.real):a.kind==Kind::String?'"'+a.text+'"':
                    a.kind==Kind::File?QString("file[%1]").arg(a.size):QString("[%1 values]").arg(a.kind==Kind::Ints?a.integers.size():a.reals.size()));
            }
            stream<<"\n";
        }
        const auto background=theme.background(),preview=theme.preview();
        if(!background.isNull())background.save(out.filePath("background.png"));
        if(!preview.isNull())preview.save(out.filePath("preview.png"));
        const auto icons=theme.icons();const auto sounds=theme.sounds();
        QDir().mkpath(out.filePath("icons"));QDir().mkpath(out.filePath("sounds"));
        for(auto it=icons.cbegin();it!=icons.cend();++it)it.value().save(out.filePath("icons/"+it.key()+".png"));
        for(auto it=sounds.cbegin();it!=sounds.cend();++it){QFile wav(out.filePath("sounds/"+it.key()+".wav"));if(wav.open(QIODevice::WriteOnly))wav.write(it.value());}
        qInfo().noquote()<<"p3t:"<<theme.name()<<"by"<<theme.author()<<"-"<<theme.elements().size()<<"elements, background"<<background.size()
            <<(theme.dynamic()?"(dynamic)":"")<<","<<icons.size()<<"icons,"<<sounds.size()<<"sounds";
        return 0;
    }
    if(parser.isSet("pad-check")) {
        // Local verification only (audit/SESSION-POWER.md): documented synthetic input reports through the exact
        // parser the HID watcher uses. The battery byte sits at payload byte 52 on a DualSense and 29 on a
        // DualShock 4, where the payload starts after the report id (and after the one/two extra Bluetooth bytes
        // the DualSense 0x31 / DualShock 4 0x11 reports carry). The last case is deliberately too short to hold
        // that byte: its buttons still parse, but batteryRead is false and the level stays unread (-1) instead of
        // reading past the end of the report.
        const auto report=[](int size,int id,std::initializer_list<std::pair<int,int>> fields){
            QVector<quint8> bytes(size,0);bytes[0]=quint8(id);
            for(const auto& [at,value]:fields)bytes[at]=quint8(value);
            return bytes;
        };
        struct Case { QString name; xmb::pad::Kind kind; QVector<quint8> report; };
        const QVector<Case> cases{
            {QStringLiteral("dualsense-usb"),xmb::pad::Kind::DualSense,report(64,0x01,{{1+52,0x07}})},       // level 7, discharging
            {QStringLiteral("dualsense-bluetooth"),xmb::pad::Kind::DualSense,report(78,0x31,{{2+52,0x20}})}, // level 0, full
            {QStringLiteral("dualshock4-usb"),xmb::pad::Kind::Ds4,report(64,0x01,{{1+29,0x17}})},            // level 7 + cable
            {QStringLiteral("dualshock4-bluetooth"),xmb::pad::Kind::Ds4,report(78,0x11,{{3+29,0x06}})},      // level 6, discharging
            {QStringLiteral("dualsense-truncated"),xmb::pad::Kind::DualSense,report(40,0x01,{})}};
        QJsonArray results;
        for(const auto& entry:cases) {
            xmb::pad::Info info;const bool parsed=xmb::pad::parseReport(entry.kind,entry.report,info);
            results.push_back(QJsonObject{{"name",entry.name},{"bytes",entry.report.size()},{"parsed",parsed},
                {"batteryRead",!info.charging.isEmpty()},
                {"kind",xmb::pad::kindName(info.kind)},{"connection",xmb::pad::transportName(info.transport)},
                {"batteryPercent",info.batteryPercent},{"charging",info.charging}});
            qInfo().noquote()<<QString("pad-check: %1 %2 bytes -> %3, %4 over %5, battery %6%7")
                .arg(entry.name).arg(entry.report.size()).arg(parsed?QStringLiteral("parsed"):QStringLiteral("rejected"))
                .arg(xmb::pad::kindName(info.kind),xmb::pad::transportName(info.transport))
                .arg(info.batteryPercent<0?QStringLiteral("not reported"):QString::number(info.batteryPercent))
                .arg(info.charging.isEmpty()?QString():QString(" (")+info.charging+")");
        }
        QFile file(parser.value("pad-check"));
        const auto bytes=QJsonDocument(QJsonObject{{"source","synthetic"},{"verifiedHardware",false},{"cases",results}}).toJson();
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
        return 0;
    }
    if(parser.isSet("pad-report")) {
        // The physical path: the HID watcher opens the controller and the report it sends is the evidence. The
        // wait covers the watcher's 2 s enumeration plus one report interval.
        xmb::pad::start();
        QTimer::singleShot(2500,&app,[&app,&parser]{
            xmb::pad::Info info;const bool attached=xmb::pad::info(info);
            QJsonObject out{{"source","hid"},{"verifiedHardware",attached},{"attached",attached},
                {"kind",xmb::pad::kindName(info.kind)},{"connection",xmb::pad::transportName(info.transport)},
                {"batteryPercent",info.batteryPercent},{"charging",info.charging}};
            if(!attached)out.insert("note","no PlayStation controller reported in 2.5 s");
            QFile file(parser.value("pad-report"));
            const auto bytes=QJsonDocument(out).toJson();
            if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();app.exit(2);return;}
            qInfo().noquote()<<QString("pad-report: attached=%1, %2 over %3, battery %4%5")
                .arg(attached?1:0).arg(xmb::pad::kindName(info.kind),xmb::pad::transportName(info.transport))
                .arg(info.batteryPercent<0?QStringLiteral("not reported"):QString::number(info.batteryPercent))
                .arg(info.charging.isEmpty()?QString():QString(" (")+info.charging+")");
            app.exit(attached?0:1);
        });
        return app.exec();
    }
    const QString data=parser.isSet("data-dir")?parser.value("data-dir"):QDir(QCoreApplication::applicationDirPath()).filePath("data");
    if(parser.isSet("steam-root"))qputenv("XMB_STEAM_ROOT",parser.value("steam-root").toUtf8());
    if(parser.isSet("steam-downloads-report")) {
        QString error;
        const auto downloads=xmb::scanSteamDownloads(parser.value("steam-root"),&error);
        QJsonArray rows;
        for(const auto& download:downloads)
            rows.push_back(QJsonObject{{"appId",download.appId},{"title",download.title},
                {"state",download.state},{"text",xmb::steamDownloadStateText(download)},
                {"bytesDownloaded",double(download.bytesDownloaded)},{"bytesToDownload",double(download.bytesToDownload)},
                {"bytesStaged",double(download.bytesStaged)},{"bytesToStage",double(download.bytesToStage)},
                {"fraction",download.fraction()},{"active",download.active},{"paused",download.paused},{"queued",download.queued}});
        const auto bytes=QJsonDocument(QJsonObject{{"downloads",rows},{"count",rows.size()},{"error",error}}).toJson();
        QFile file(parser.value("steam-downloads-report"));
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
        qInfo().noquote()<<QString("steam downloads: %1 entr%2%3").arg(rows.size()).arg(rows.size()==1?"y":"ies")
            .arg(error.isEmpty()?QString():QStringLiteral(" (")+error+QStringLiteral(")"));
        if(qEnvironmentVariableIsSet("XMB_STEAM_DOWNLOADS_LOG"))xmb::logSteamDownloads(downloads);
        return 0;
    }
    if(parser.isSet("steam-accounts-report")) {
        QString error;
        const auto accounts=xmb::scanSteamAccounts(parser.value("steam-root"),&error);
        QJsonArray rows;
        int activeCount=0;
        QString steamExe;
        for(const auto& account:accounts) {
            const auto plan=xmb::steamAccountSwitchPlan(account,parser.value("steam-root"));
            if(account.active)++activeCount;
            if(steamExe.isEmpty())steamExe=plan.steamExe;
            rows.push_back(QJsonObject{{"steamId64",account.steamId64},{"accountName",account.accountName},
                {"personaName",account.personaName},{"active",account.active},{"autoLogin",account.autoLogin},
                {"hasUserdata",account.hasUserdata},{"lastLoginUnix",double(account.lastLoginUnix)},
                {"switchPossible",plan.possible},{"alreadyActive",plan.alreadyActive},
                {"clientSignedIn",plan.clientSignedIn}});
        }
        const auto bytes=QJsonDocument(QJsonObject{{"accounts",rows},{"count",rows.size()},{"steamExe",steamExe},
            {"shutdownArguments",QJsonArray::fromStringList(xmb::steamShutdownArguments())},
            {"relaunchArguments",QJsonArray::fromStringList(xmb::steamRelaunchArguments())},
            {"error",error}}).toJson();
        QFile file(parser.value("steam-accounts-report"));
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
        qInfo().noquote()<<QString("steam accounts: %1 remembered, %2 active").arg(rows.size()).arg(activeCount);
        return 0;
    }
    if(parser.isSet("import-apps")) {
        QString error;
        if(!xmb::ContentRegistry::importFile(parser.value("import-apps"),QDir(data).filePath("apps.json"),error)){qCritical().noquote()<<error;return 2;}
        qInfo()<<"Apps imported successfully.";return 0;
    }
    if(parser.isSet("steam-report")) {
        const auto steam=xmb::scanSteam();QJsonArray games,warnings;
        for(const auto& game:steam.games)games.push_back(QJsonObject{{"appId",game.appId},{"title",game.title},{"installDirectory",game.installDirectory},{"artwork",game.artwork}});
        for(const auto& warning:steam.warnings)warnings.push_back(warning);
        const auto bytes=QJsonDocument(QJsonObject{{"games",games},{"warnings",warnings},{"count",games.size()}}).toJson();
        QFile file(parser.value("steam-report"));
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
        return 0;
    }
    if(parser.isSet("steam-friends-report")) {
        xmb::SteamFriends friends;
        QObject::connect(&friends,&xmb::SteamFriends::loaded,&app,[&](const xmb::SteamFriendList& people,const QString& error){
            QJsonArray entries;
            for(const auto& person:people)entries.push_back(QJsonObject{{"steamId",person.steamId},{"name",person.name},{"game",person.game},{"personaState",person.personaState}});
            QFile file(parser.value("steam-friends-report"));const auto bytes=QJsonDocument(QJsonObject{{"friends",entries},{"error",error},{"source",friends.source()}}).toJson();
            if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();app.exit(2);return;}
            app.exit(error.isEmpty()?0:2);
        });
        const auto id=qEnvironmentVariable("XMB_STEAM_ID",xmb::scanSteam().steamId);
        QTimer::singleShot(0,&app,[&friends,id]{friends.refresh(id);});
        return app.exec();
    }
    if(parser.isSet("steam-family-report")) {
        const auto family=xmb::scanSteamFamily(qEnvironmentVariable("XMB_STEAM_ID"));
        QString familyError;
        const auto shared=xmb::scanFamilySharedGames(data,&familyError);
        QJsonArray members;for(const auto& member:family.memberAccountIds)members.push_back(member);
        QJsonArray games;
        for(const auto& game:shared)games.push_back(QJsonObject{{"appId",game.appId},{"title",game.title},
            {"install",xmb::SteamOwned::installUrl(game.appId)}});
        QFile file(parser.value("steam-family-report"));
        const auto bytes=QJsonDocument(QJsonObject{{"valid",family.valid},{"groupId",family.groupId},{"name",family.name},
            {"members",members},{"shared",games},{"sharedCount",games.size()},{"error",familyError}}).toJson();
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
        return 0;
    }
    if(parser.isSet("steam-artwork-report")) {
        const auto overrides=xmb::scanSteamArtworkOverrides(data);
        QJsonArray rows;
        for(const auto& entry:overrides)rows.push_back(QJsonObject{{"appId",entry.appId},{"row",entry.row},
            {"icon",entry.icon},{"background",entry.background}});
        QFile file(parser.value("steam-artwork-report"));
        const auto bytes=QJsonDocument(QJsonObject{{"artwork",rows},{"count",rows.size()}}).toJson();
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
        return 0;
    }
    if(parser.isSet("steam-library-report")) {
        // Purchased games (Steam Web API, needs STEAM_WEB_API_KEY) grouped by the account's own
        // Steam collections, plus an "All games" fallback when no key is configured.
        xmb::SteamOwned owned;
        QObject::connect(&owned,&xmb::SteamOwned::loaded,&app,[&](const xmb::OwnedGameList& games,const QString& error,bool needsKey){
            QString collectionsError;
            const auto collections=xmb::scanSteamCollections(xmb::scanSteam().steamId,&collectionsError);
            QHash<QString,QVector<const xmb::OwnedGame*>> byCollection;
            QSet<QString> placed;
            for(const auto& collection:collections)
                for(const auto& appId:collection.appIds)
                    for(const auto& game:games)
                        if(game.appId==appId){byCollection[collection.id].push_back(&game);placed.insert(appId);}
            QJsonArray groups;
            for(const auto& collection:collections) {
                QJsonArray rows;
                for(const auto* game:byCollection.value(collection.id))
                    rows.push_back(QJsonObject{{"appId",game->appId},{"title",game->title},{"installed",game->installed},
                        {"playtimeMinutes",game->playtimeMinutes},{"header",game->headerUrl},{"icon",game->iconUrl},
                        {"install",xmb::SteamOwned::installUrl(game->appId)},{"launch",xmb::SteamOwned::launchUrl(game->appId)}});
                groups.push_back(QJsonObject{{"id",collection.id},{"name",collection.name},{"games",rows},{"count",rows.size()}});
            }
            QJsonArray ungrouped;
            for(const auto& game:games) if(!placed.contains(game.appId))
                ungrouped.push_back(QJsonObject{{"appId",game.appId},{"title",game.title},{"installed",game.installed},
                    {"header",game.headerUrl},{"install",xmb::SteamOwned::installUrl(game.appId)},
                    {"launch",xmb::SteamOwned::launchUrl(game.appId)}});
            QFile file(parser.value("steam-library-report"));
            const auto bytes=QJsonDocument(QJsonObject{{"steamId",xmb::scanSteam().steamId},{"owned",games.size()},
                {"collections",groups},{"allGames",ungrouped},
                {"error",error.isEmpty()?collectionsError:error},{"needsApiKey",needsKey}}).toJson();
            if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();app.exit(2);return;}
            app.exit(error.isEmpty()?0:2);
        });
        QTimer::singleShot(0,&app,[&owned]{owned.refresh(qEnvironmentVariable("XMB_STEAM_ID"));});
        return app.exec();
    }    if(parser.isSet("steam-collections-report")) {
        QString error;
        const auto collections=xmb::scanSteamCollections(qEnvironmentVariable("XMB_STEAM_ID"),&error);
        QJsonArray rows;
        for(const auto& collection:collections) {
            QJsonArray ids;for(const auto& id:collection.appIds)ids.push_back(id);
            rows.push_back(QJsonObject{{"id",collection.id},{"name",collection.name},{"appIds",ids},{"count",ids.size()}});
        }
        QFile file(parser.value("steam-collections-report"));
        const auto bytes=QJsonDocument(QJsonObject{{"steamId",xmb::scanSteam().steamId},{"collections",rows},{"count",rows.size()},{"error",error}}).toJson();
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
        return error.isEmpty()?0:2;
    }
    if(parser.isSet("owned-games-report")) {
        xmb::SteamOwned owned;
        QObject::connect(&owned,&xmb::SteamOwned::loaded,&app,[&](const xmb::OwnedGameList& games,const QString& error,bool needsKey){
            QJsonArray rows;
            for(const auto& game:games)rows.push_back(QJsonObject{{"appId",game.appId},{"title",game.title},{"installed",game.installed},
                {"playtimeMinutes",game.playtimeMinutes},{"icon",game.iconUrl},{"header",game.headerUrl},
                {"install",xmb::SteamOwned::installUrl(game.appId)},{"launch",xmb::SteamOwned::launchUrl(game.appId)},
                {"storePage",xmb::SteamOwned::storeUrl(game.appId)}});
            QFile file(parser.value("owned-games-report"));
            const auto bytes=QJsonDocument(QJsonObject{{"games",rows},{"count",rows.size()},{"error",error},{"needsApiKey",needsKey}}).toJson();
            if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();app.exit(2);return;}
            app.exit(error.isEmpty()?0:2);
        });
        QTimer::singleShot(0,&app,[&owned]{owned.refresh(qEnvironmentVariable("XMB_STEAM_ID"));});
        return app.exec();
    }
    if(parser.isSet("steam-store-report")) {
        xmb::SteamStore store;
        QObject::connect(&store,&xmb::SteamStore::loaded,&app,[&](const xmb::StoreItemList& items,const QString& query,const QString& error){
            QJsonArray rows;
            for(const auto& item:items)rows.push_back(QJsonObject{{"appId",item.appId},{"title",item.title},{"image",item.imageUrl},
                {"price",xmb::SteamStore::priceText(item)},{"discounted",item.discounted},{"windows",item.windows},
                {"install",xmb::SteamStore::installUrl(item.appId)},{"storePage",xmb::SteamStore::storeUrl(item.appId)}});
            QFile file(parser.value("steam-store-report"));
            const auto bytes=QJsonDocument(QJsonObject{{"query",query},{"items",rows},{"count",rows.size()},{"error",error}}).toJson();
            if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();app.exit(2);return;}
            app.exit(error.isEmpty()?0:2);
        });
        const auto term=parser.value("steam-store-query");
        QTimer::singleShot(0,&app,[&store,term]{if(term.isEmpty())store.topSellers();else store.search(term);});
        return app.exec();
    }
    if(!parser.isSet("dev-flash"))parser.showHelp(2);
    if(parser.isSet("inventory")) {
        const auto inventory=xmb::inventoryXmb(parser.value("dev-flash"));
        if(inventory.value("documentCount").toInt()==0){qCritical()<<"No local XMBML files found";return 2;}
        QFile output(parser.value("inventory"));
        if(!output.open(QIODevice::WriteOnly)){qCritical()<<output.errorString();return 2;}
        const auto bytes=QJsonDocument(inventory).toJson();
        if(output.write(bytes)!=bytes.size()){qCritical()<<output.errorString();return 2;}
        qInfo()<<"catalog inventory:"<<inventory.value("documentCount").toInt()<<"documents,"<<inventory.value("viewCount").toInt()<<"views,"<<inventory.value("itemCount").toInt()<<"items,"<<inventory.value("errorCount").toInt()<<"errors";
        return inventory.value("errorCount").toInt()?1:0;
    }
    xmb::Assets assets(parser.value("dev-flash"));
    if(parser.isSet("motion-report")) {
        QJsonArray cases;
        for(const auto& spec:{std::pair{5,200},std::pair{4,300}}) {
            QJsonArray rates;
            for(int hz:{60,120,240}) {
                QJsonArray samples;
                for(double ms:{25.,50.,100.,150.,175.,200.,300.}) {
                    xmb::View::PafEase ease;ease.retarget(0);ease.retarget(1,spec.second,spec.first);
                    double elapsed=0;const double end=ms/1000.,tick=1./hz;
                    while(elapsed<end){const double step=std::min(tick,end-elapsed);ease.advance(step);elapsed+=step;}
                    samples.push_back(QJsonObject{{"milliseconds",ms},{"value",ease.value}});
                }
                xmb::View::PafEase interrupted;interrupted.retarget(0);interrupted.retarget(1,spec.second,spec.first);
                interrupted.advance(.083);const double from=interrupted.value;interrupted.retarget(0,spec.second,spec.first);
                const double retargeted=interrupted.value;interrupted.advance(.5);
                rates.push_back(QJsonObject{{"hz",hz},{"samples",samples},{"retargetFrom",from},
                    {"retargetValue",retargeted},{"afterLongTick",interrupted.value}});
            }
            cases.push_back(QJsonObject{{"mode",spec.first},{"durationMs",spec.second},{"rates",rates}});
        }
        QFile out(parser.value("motion-report"));const auto bytes=QJsonDocument(QJsonObject{{"cases",cases}}).toJson();
        if(!out.open(QIODevice::WriteOnly) || out.write(bytes)!=bytes.size()){qCritical()<<out.errorString();return 2;}
        return 0;
    }
    if(parser.isSet("dev-hdd0"))assets.setHddRoot(parser.value("dev-hdd0"));
    if(parser.isSet("vsh-modules"))assets.setModulesRoot(parser.value("vsh-modules"));
    if(parser.isSet("settings-report")) {
        // The whole Settings surface as one JSON document (audit/SETTINGS-COVERAGE.md 5.1): the machine
        // snapshot, the RCO container every label was read from, the choice model's provenance, and every
        // page row's live value, where that value came from and its coverage verdict. No window is opened,
        // nothing on the machine is written, and the only file is the one named here.
        xmb::Preferences preferences(data);
        auto report=xmb::settingsReport(assets,preferences,data);
        if(parser.isSet("settings-page")) {
            const auto only=parser.value("settings-page");
            QJsonArray kept;
            for(const auto& page:report.value("pages").toArray())
                if(page.toObject().value("id").toString()==only)kept.push_back(page);
            if(kept.isEmpty()){qCritical().noquote()<<"settings-report: no page named"<<only;return 2;}
            report["pages"]=kept;
            report["pageFilter"]=only;
        }
        QFile file(parser.value("settings-report"));
        const auto bytes=QJsonDocument(report).toJson();
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical()<<file.errorString();return 2;}
        const auto totals=report.value("totals").toObject();
        qInfo().noquote()<<QStringLiteral("settings-report: %1 inventory rows: %2 real, %3 placeholder, "
            "%4 absent; %5 rows show a value; %6 pages; %7 permitted host writes; %8 unavailable messages")
            .arg(totals.value("rows").toInt()).arg(totals.value("real").toInt())
            .arg(totals.value("placeholder").toInt()).arg(totals.value("absent").toInt())
            .arg(totals.value("withValue").toInt()).arg(report.value("pages").toArray().size())
            .arg(report.value("writeSurface").toArray().size())
            .arg(report.value("unavailableMessageCount").toInt());
        return 0;
    }
    if(parser.isSet("export-image")) { // local analysis: container:name[,container:name...] -> <dir>/<name>.png
        const auto spec=parser.value("export-image").split('@');
        if(spec.size()!=2){qCritical()<<"use --export-image container:name,...@directory";return 2;}
        QDir().mkpath(spec[1]);int failures=0;
        for(const auto& item:spec[0].split(',')) {
            const auto parts=item.split(':');if(parts.size()!=2){++failures;continue;}
            const auto& image=assets.image(parts[0],parts[1]);
            if(image.isNull() || !image.save(QDir(spec[1]).filePath(parts[1]+".png"))){qWarning()<<"cannot export"<<item;++failures;}
            else qInfo()<<"exported"<<item<<image.size();
        }
        return failures?1:0;
    }
    if(parser.isSet("rco-text")) { // local analysis: container,container...@file -> "container id = text" lines
        const auto spec=parser.value("rco-text").split('@');
        if(spec.size()!=2){qCritical()<<"use --rco-text container,...@file";return 2;}
        QFile out(spec[1]);if(!out.open(QIODevice::WriteOnly|QIODevice::Text)){qCritical()<<out.errorString();return 2;}
        QTextStream stream(&out);
        for(const auto& container:spec[0].split(',')) {
            try {
                const auto strings=assets.allText(container);auto keys=strings.keys();keys.sort();
                for(const auto& key:keys)stream<<container<<" "<<key<<" = "<<QString(strings[key]).replace('\n',"\\n")<<"\n";
            } catch(const std::exception& e) { stream<<container<<": "<<e.what()<<"\n"; }
        }
        return 0;
    }
    if(parser.isSet("rco-images")) { // local analysis: every image name in every vsh/resource RCO
        QFile out(parser.value("rco-images"));
        if(!out.open(QIODevice::WriteOnly|QIODevice::Text)){qCritical()<<out.errorString();return 2;}
        QTextStream stream(&out);
        for(const auto& file:QDir(QDir(parser.value("dev-flash")).filePath("vsh/resource")).entryInfoList({"*.rco"},QDir::Files,QDir::Name)) {
            QStringList names;
            try { names=assets.imageNames(file.completeBaseName()); } catch(const std::exception& e) { stream<<file.completeBaseName()<<": "<<e.what()<<"\n";continue; }
            names.sort();stream<<file.completeBaseName()<<": "<<names.join(' ')<<"\n";
        }
        return 0;
    }
    if(parser.isSet("rco-tree")) {
        // Local analysis (P1.1): the RCO object model, read from the firmware's own bytes at run time.
        // "<container>[,<container>...][@file]"; "*" dumps every vsh/resource RCO. Each container's read
        // cost is measured here so the audit can cite it (a repeated name shows the cache: 0.00 ms).
        const auto spec=parser.value("rco-tree").split('@');
        if(spec.size()>2 || spec[0].trimmed().isEmpty()){qCritical()<<"use --rco-tree container[,container...][@file]";return 2;}
        QElapsedTimer total;total.start();
        xmb::RcoObjects objects(assets);
        QStringList names;
        for(const auto& name:spec[0].split(',',Qt::SkipEmptyParts))names<<name.trimmed();
        if(names.size()==1 && (names[0]=="*" || names[0]=="all"))names=objects.containers();
        QFile out;
        if(spec.size()==2) {
            out.setFileName(spec[1]);
            if(!out.open(QIODevice::WriteOnly|QIODevice::Text)){qCritical()<<out.errorString();return 2;}
        }
        const auto writeLine=[&](const QString& line){if(out.isOpen())out.write(line.toUtf8()+'\n');else qInfo().noquote()<<line;};
        // Firmware strings are rendered ASCII-only (\uXXXX), so a dump can be quoted in an ASCII document.
        const auto escape=[](const QString& text){
            QString result;
            for(const QChar c:text) {
                const ushort code=c.unicode();
                if(code>=0x20 && code<0x7f)result+=c;
                else if(code=='\n')result+="\\n";
                else result+=QString("\\u%1").arg(code,4,16,QChar('0'));
            }
            return result;
        };
        const auto writeEntry=[&](const xmb::RcoObjects::Container& container,const QString& indent,const QString& type,
                                  const QString& name,quint32 offset,const QByteArray& extra,
                                  const QVector<xmb::RcoObjects::Property>& properties) {
            using Kind=xmb::RcoObjects::ValueKind;
            using RefKind=xmb::RcoObjects::RefKind;
            QString line=indent+type;
            line+=name.isEmpty()?QString(" (anon)"):QString(" ")+name;
            line+=QString(" @%1 +%2B").arg(offset).arg(extra.size());
            int named=0;
            for(const auto& property:properties) {
                const auto& value=property.value;
                named+=value.kind==Kind::Float || value.kind==Kind::Int || value.kind==Kind::Unknown?4:8;
                line+=' ';line+=property.name;line+='=';
                if(value.kind==Kind::Float)line+=QString::number(value.real,'g',8);
                else if(value.kind==Kind::Int)line+=QString::number(value.integer);
                else if(value.kind==Kind::Unknown)line+=QString("0x")+QString::number(value.integer,16);
                else {
                    line+=escape(container.resolve(value));
                    if(value.refKind()==RefKind::Text) {
                        const auto text=container.text(value.target);
                        if(!text.isEmpty())line+=QString("=\"%1\"").arg(escape(text));
                    }
                }
            }
            if(extra.size()>named) { // bytes past the schema fields: the entry's own float series
                QStringList values;
                for(float value:xmb::RcoObjects::Container::floatsOf(extra.mid(named)))values<<QString::number(value,'g',8);
                line+=QString(" tail=[%1]").arg(values.join(','));
            }
            writeLine(line);
        };
        int failures=0,objectCount=0,animationCount=0;
        for(const auto& name:names) {
            const qint64 before=objects.cost().nanoseconds;
            const auto& container=objects.container(name);
            const double readMs=double(objects.cost().nanoseconds-before)/1e6;
            if(!container.ok) {
                ++failures;
                writeLine(QString("# rco-tree %1: ERROR %2").arg(name,container.error));
                continue;
            }
            objectCount+=container.objects.size();animationCount+=container.animations.size();
            writeLine(QString("# rco-tree %1 version=0x%2 compression=%3 file=%4 tables=%5 objects=%6 animations=%7 read=%8ms")
                .arg(name,QString::number(container.version,16),QString::number(container.compression),
                     QString::number(container.fileBytes),QString::number(container.tableBytes))
                .arg(container.objects.size()).arg(container.animations.size()).arg(QString::number(readMs,'f',2)));
            for(const auto& entry:container.objects)
                writeEntry(container,QString(entry.depth*2,' '),entry.type,entry.name,entry.offset,container.extra(entry),
                           container.properties(entry));
            for(const auto& entry:container.animations)
                writeEntry(container,QString(entry.depth*2,' '),entry.type,entry.name,entry.offset,container.extra(entry),
                           container.properties(entry));
        }
        const auto cost=objects.cost();
        writeLine(QString("# rco-tree: %1 containers, %2 objects, %3 animations, %4 bytes read, read+parse %5 ms, total %6 ms")
            .arg(cost.containers).arg(objectCount).arg(animationCount).arg(cost.bytesRead)
            .arg(QString::number(double(cost.nanoseconds)/1e6,'f',2)).arg(total.elapsed()));
        if(out.isOpen())out.close();
        return failures?1:0;
    }
    if(parser.isSet("image-audit")) {
        // Headless resolution table (audit/ICON-GAP-AUDIT.md section 9): every image name the B/C inventory
        // names, the container that actually ships it, its decoded size and pixel count, the container the
        // call site asked for, whether a container-override rule migrated it, and the bestImage gate result
        // for the three allow-listed names. An optional positional argument writes the table to a file
        // instead of stdout (a GUI-subsystem build has no console to print to).
        return runImageAudit(assets,parser.positionalArguments().value(0));
    }

    if(parser.isSet("month-sheet")) { // local analysis only: firmware-derived pixels, keep out of Git
        constexpr int scale=6;QImage sheet;QPainter painter;
        for(int m=1;m<=12;++m)for(int kind=0;kind<2;++kind) {
            const auto& image=assets.image("qgl/lines",QString("textures/month_bg/%1/%2.dds").arg(kind?"night":"rgb").arg(m,2,10,QChar('0')));
            if(image.isNull())continue;
            if(sheet.isNull()){sheet=QImage(image.width()*scale*2+30,(image.height()*scale+20)*12,QImage::Format_RGB32);sheet.fill(Qt::black);painter.begin(&sheet);painter.setPen(Qt::white);}
            const QRect target(kind*(image.width()*scale+30),(m-1)*(image.height()*scale+20),image.width()*scale,image.height()*scale);
            painter.drawImage(target,image);painter.drawText(target.adjusted(4,2,0,0),Qt::AlignLeft|Qt::AlignTop,QString("%1 %2").arg(m).arg(kind?"night":"day"));
        }
        if(painter.isActive())painter.end();
        if(sheet.isNull() || !sheet.save(parser.value("month-sheet"))){qCritical()<<"Cannot write month sheet";return 2;}
        return 0;
    }
    if(parser.isSet("icon-sheet")) {
        QImage sheet(1400,1200,QImage::Format_RGB32);sheet.fill(QColor(45,40,25));QPainter painter(&sheet);
        painter.setPen(Qt::white);
        for(int i=1;i<=70;++i) {
            const auto name=QString::number(i)+"n";const auto& icon=assets.image("qgl/icontex",name);
            const int x=((i-1)%10)*140,y=((i-1)/10)*170;
            painter.drawImage(QRect(x+6,y+6,128,128),icon);painter.drawText(QRect(x,y+135,140,30),Qt::AlignCenter,name);
        }
        painter.end();
        if(!sheet.save(parser.value("icon-sheet"))){qCritical()<<"Cannot save icon sheet";return 2;}
        return assets.failedImages()?1:0;
    }
    if(parser.isSet("font-report")) {
        QFile out(parser.value("font-report"));
        if(!out.open(QIODevice::WriteOnly|QIODevice::Text)){qCritical()<<out.errorString();return 2;}
        xmb::FontRaster raster;
        if(!raster.configure(QDir(assets.root()).filePath("data/font"))){qCritical()<<"Cannot load firmware font faces";return 2;}
        out.write((raster.version()+"; coverage flags=10; face identity=head+cmap\n").toUtf8());
        const QStringList samples{QString::fromUtf16(u"Saved Data Utility"),QString::fromUtf16(u"Paramètres réseau Ženska Ç ő ł"),
            QString::fromUtf16(u"○ ✕ △ □  ←↑→↓ ©™"),
            QString::fromUtf16(u"\uf8fc \uf8fd \uf8fe \uf8ff"),QString::fromUtf16(u"AV To ff fi ffi e\u0301"),
            QString::fromUtf16(u"ゲーム 設定"),QString::fromUtf16(u"게임 설정"),QString::fromUtf16(u"游戏 设置")};
        bool windowsFallback=false;
        for(const auto& family:assets.fontFamilies()) {
            out.write(QString("== %1\n").arg(family).toUtf8());
            for(const auto& sample:samples) {
                QFont font(family);font.setPixelSize(28);font.setStyleName("Regular");font.setHintingPreference(QFont::PreferNoHinting);
                QTextLayout layout(xmb::FontRaster::mapSymbols(sample),font);layout.beginLayout();layout.createLine();layout.endLayout();
                QStringList runs;
                for(const auto& run:layout.glyphRuns()) {
                    const auto raw=run.rawFont();const QString source=raster.sourceFor(raw);
                    runs<<QString("%1 / %2 (%3 glyphs; %4)").arg(raw.familyName(),raw.styleName()).arg(run.glyphIndexes().size())
                        .arg(source.isEmpty()?QString("UNMATCHED HOST FACE"):QFileInfo(source).fileName());
                    if(source.isEmpty())windowsFallback=true;
                }
                out.write(QString("  %1  ->  %2\n").arg(sample,runs.join(" + ")).toUtf8());
            }
        }
        return windowsFallback?1:0;
    }
    if(parser.isSet("decode-fp")) {
        // Local analysis: "archive:lib/x.fpo@dir". Every <dir>/*.ucode the size of that program's microcode (an RSX
        // capture, constants patched in by the Cg runtime) is put back into the .fpo and its named parameters' values
        // are written to <same name>.params.txt.
        const auto spec=parser.value("decode-fp").split('@');
        if(spec.size()!=2 || !spec[0].contains(':')){qCritical()<<"use --decode-fp archive:lib/x.fpo@directory";return 2;}
        QByteArray binary;
        try { binary=assets.qglFile(spec[0].section(':',0,0),spec[0].section(':',1)); } catch(const std::exception& e){qCritical()<<e.what();return 2;}
        const quint32 ucodeSize=qFromBigEndian<quint32>(binary.constData()+24),ucodeOffset=qFromBigEndian<quint32>(binary.constData()+28);
        int decoded=0;
        for(const auto& info:QDir(spec[1]).entryInfoList({"*.ucode"},QDir::Files,QDir::Name)) {
            QFile in(info.filePath());if(!in.open(QIODevice::ReadOnly))continue;
            const QByteArray ucode=in.readAll();if(quint32(ucode.size())!=ucodeSize)continue;
            QByteArray patched=binary;patched.replace(int(ucodeOffset),int(ucodeSize),ucode);
            QString error;const auto program=xmb::CgProgram::load(patched,&error);
            const auto original=xmb::CgProgram::load(binary);
            if(!program.valid() || program.instructionCount!=original.instructionCount)continue; // a different program of the same size
            QFile out(info.path()+"/"+info.completeBaseName()+".params.txt");if(!out.open(QIODevice::WriteOnly|QIODevice::Text))continue;
            QTextStream text(&out);
            auto names=program.parameters.keys();names.sort();
            for(const auto& name:names) {
                const auto& p=program.parameters[name];if(p.constantSlots.isEmpty())continue;
                const auto& c=program.constants[p.constantSlots.first()];
                text<<name<<" = "<<c.x()<<" "<<c.y()<<" "<<c.z()<<" "<<c.w()<<"\n";
            }
            ++decoded;
        }
        qInfo()<<"decoded"<<decoded<<"captures of"<<spec[0];
        return decoded?0:1;
    }
    if(parser.isSet("dump-cg")) {
        // Local analysis only: the folder receives firmware-derived shader text and must stay out of Git.
        int failures=0;
        for(const QString entry:{"moyou/background","moyou/back_colours0","moyou/back_colours1","moyou/back_colours2","moyou/LinesController","moyou/LinesControllerQuin",
                                "moyou/LinesEdge","moyou/lines0","moyou/lines1","moyou/lines2","moyou/ffd_shader0","moyou/ffd_shader1","moyou/ffd_shader2","moyou/ffd_shader3",
                                "moyou/ffd_alpha_blend","glutils/Copy","glutils/GlareSource","glutils/GlareSourcePre","glutils/GlareSourceNoTone","glutils/Gaussian",
                                "glutils/ApplyDisplay","glutils/ToneApplyDisplay","glutils/AccGlare","glutils/DoubleGaussian","glutils/GaussBloomAndBlur","glutils/BlurAcc",
                                "glutils/BloomCone","glutils/ToneMap","glutils/ApplyGlare","particles/particles_quads","particles/particles_second",
                                "icons:icons/quad","icons:icons/mask","icons:icons/icons_full_ss","icons:icons/icons_full_ss_no_shad","icons:icons/icons_cheap_ss",
                                "icons:icons/icons_cheap_ss_no_shad","icons:icons/icons_no_ss","icons:icons/icons_no_ss_no_shad","raf:raf/vert0","raf:raf/sce01"})
            for(const QString ext:{"vpo","fpo"}) {
                QByteArray bytes;
                const QString archive=entry.contains(':')?entry.section(':',0,0):QString("lines"),name=entry.section(':',-1);
                try { bytes=assets.qglFile(archive,"lib/"+name+"."+ext); } catch(const std::exception&) { continue; } // not every program has both stages
                if(bytes.isEmpty())continue;
                QString error;const auto program=xmb::CgProgram::load(bytes,&error);
                if(!program.valid()){qCritical()<<name<<ext<<error;++failures;continue;}
                QFile out(QDir(parser.value("dump-cg")).filePath(name.section('/',-1)+"."+ext+".glsl"));
                if(!QDir().mkpath(parser.value("dump-cg")) || !out.open(QIODevice::WriteOnly|QIODevice::Text)){++failures;continue;}
                QString header=QString("// %1.%2: %3 instructions, ucode %4 bytes\n").arg(name,ext).arg(program.instructionCount).arg(qFromBigEndian<quint32>(bytes.constData()+24));
                for(const auto& p:program.parameters)header+=QString("// param %1 :%2 res=%3 idx=%4 slots=%5 defaults=%6\n").arg(p.name,p.semantic).arg(p.resource).arg(p.resourceIndex)
                    .arg([&]{QStringList l;for(int v:p.constantSlots)l<<QString::number(v);return l.join(',');}())
                    .arg([&]{QStringList l;for(float v:p.defaults)l<<QString::number(v);return l.join(',');}());
                for(int i=0;i<program.constants.size();++i){const auto& c=program.constants[i];header+=QString("// fc[%1] = %2 %3 %4 %5\n").arg(i).arg(c.x()).arg(c.y()).arg(c.z()).arg(c.w());}
                out.write((header+program.glsl).toUtf8());
            }
        return failures?1:0;
    }
    // ---- Scripted-run input gate (audit/PS-BUTTON.md) ----
    // A scripted capture must be deterministic: the script drives and a live controller must not. One pad whose
    // left stick rested off centre reported a held direction on every poll and undid every scripted DOWN (188
    // vertical(-1) calls in one capture), so --script - or XMB_INPUT_SCRIPT_ONLY=1 on a run without it - turns
    // the gate on. pad::setScriptOnly() then withholds every live sample from the shell and the PS watcher and
    // counts what it withheld (the sidecar's padEventsIgnored); with neither flag the pad behaves exactly as
    // before. The gate is set here because the View's constructor starts the pad reader and the PS watcher.
    const bool scriptOnlyEnv=qEnvironmentVariable("XMB_INPUT_SCRIPT_ONLY")=="1";
    const bool scriptedRun=parser.isSet("script") || scriptOnlyEnv;
    xmb::pad::setScriptOnly(scriptedRun);
    if(inputTraceEnabled())
        qInfo().noquote()<<QString("input: trace gate scripted=%1 trigger=%2")
            .arg(scriptedRun?1:0)
            .arg(parser.isSet("script")?QStringLiteral("--script")
                :scriptOnlyEnv?QStringLiteral("XMB_INPUT_SCRIPT_ONLY"):QStringLiteral("none"));
    xmb::Shell shell(data,assets);
    // ---- Kiosk / launcher mode (Phase 4.2) ----
    // --kiosk is the appliance recipe: one frameless fullscreen window on one display, straight to the XMB. It
    // writes no preference, so leaving the flag (or starting without it) restores the stored window state.
    // --start-windowed is the local-testing counterpart: ignore a stored fullscreen preference for this run.
    const bool kiosk=parser.isSet("kiosk"),startWindowed=parser.isSet("start-windowed");
    if(kiosk && startWindowed){qCritical()<<"--kiosk and --start-windowed are mutually exclusive";return 2;}
    QScreen* kioskScreen=nullptr;
    if(parser.isSet("display")) {
        if(!kiosk){qCritical()<<"--display selects the screen for --kiosk; pass --kiosk as well";return 2;}
        bool ok=false;const int index=parser.value("display").toInt(&ok);
        const auto screens=QGuiApplication::screens();
        if(!ok || index<0 || index>=screens.size()) {
            qCritical().noquote()<<QString("--display must be a screen index from 0 through %1").arg(screens.isEmpty()?QStringLiteral("0"):QString::number(screens.size()-1));
            return 2;
        }
        kioskScreen=screens[index];
    }
    if(kiosk && !kioskScreen)kioskScreen=QGuiApplication::primaryScreen();
    if(kiosk && !kioskScreen){qCritical()<<"kiosk: no screen is available";return 2;}
    QSettings windowSettings(QDir(data).filePath("settings.ini"),QSettings::IniFormat);
    QMainWindow window;window.setWindowTitle("re-ps3 — Native XMB");auto* view=new xmb::View(shell,assets,&window);window.setCentralWidget(view);window.resize(960,540);
    // Kiosk starts at the XMB like a console that is already on: the cold-boot intro is a boot animation, not a
    // launcher screen, and an appliance should be usable the moment the window appears.
    view->setIntroEnabled(!parser.isSet("no-intro") && !parser.isSet("script") && !parser.isSet("background-clock") && !parser.isSet("measure-reference") && !kiosk);
    if(parser.isSet("background-clock")) {
        const auto when=QDateTime::fromString(parser.value("background-clock"),Qt::ISODate);
        if(!when.isValid()){qCritical()<<"background-clock requires an ISO date/time";return 2;}
        view->setBackgroundClock(when);
    }
    if(parser.isSet("render-clock")) {
        bool ok=false;const double seconds=parser.value("render-clock").toDouble(&ok);
        if(!ok || seconds<0){qCritical()<<"render-clock requires a non-negative number of seconds";return 2;}
        view->setRenderClock(seconds);
    }
    if(parser.isSet("ui-clock")) {
        const auto when=QDateTime::fromString(parser.value("ui-clock"),Qt::ISODate);
        if(!when.isValid()){qCritical()<<"ui-clock requires an ISO date/time";return 2;}
        shell.setClockOverride(when);
    }
    if(parser.isSet("focus-time")) {
        bool ok=false;const auto milliseconds=parser.value("focus-time").toLongLong(&ok);
        if(!ok || milliseconds<0){qCritical()<<"focus-time requires non-negative integer milliseconds";return 2;}
        view->setFocusTime(milliseconds);
    }
    if(parser.isSet("measure-reference")) {
        window.show();
        QTimer::singleShot(1000,&app,[&]{
            const bool ok=view->measureReference(parser.value("measure-reference"));
            if(!ok)qCritical()<<"Background measurement failed";
            app.exit(ok?0:1);
        });
        return app.exec();
    }
    if(windowSettings.contains("window/geometry"))window.restoreGeometry(windowSettings.value("window/geometry").toByteArray());
    // Kiosk does not write the window geometry back: the fullscreen rectangle would replace the stored window
    // state, which is a preference change a launcher flag has no business making.
    QObject::connect(&app,&QApplication::aboutToQuit,&window,[&]{if(kiosk)return;windowSettings.setValue("window/geometry",window.saveGeometry());windowSettings.sync();});
    // ---- Auto-Off (Phase 4.1) ----
    // Shell emits requestQuit from exactly two places today: the idle timer (power/idleMinutes) and the PS
    // menu's confirmed "Turn Off the System". The confirmation emits while its own flag is still up, so the
    // flag is the discriminator: a confirmed Turn Off quits, an idle Auto-Off goes dark and the XMB stays
    // running. Once shell.cpp calls host::requestDisplayOff() from the idle timer (the hunk in
    // audit/SESSION-POWER.md), this glue is unnecessary and should be deleted.
    QObject::connect(&shell,&xmb::Shell::requestQuit,&app,[&app,&shell]{
        if(shell.psMenuConfirmingOff()) {
            qInfo()<<"power: Turn Off the System confirmed; the XMB quits";
            app.quit();
            return;
        }
        if(!xmb::host::displayOffRequested())
            qInfo().noquote()<<QString("power: Auto-Off at idle %1 ms; the XMB stays running").arg(shell.idleMilliseconds());
        xmb::host::requestDisplayOff();
    });
    // Auto-Off's only honest read-back: subscribe to the console display state before the first request.
    xmb::host::observeDisplayState();
    if(!startWindowed && !kiosk)
        QObject::connect(&shell,&xmb::Shell::changed,&window,[&]{if(shell.fullscreen()!=window.isFullScreen()){if(shell.fullscreen())window.showFullScreen();else window.showNormal();}});
    if(kiosk) {
        // Frameless fullscreen on the chosen screen: showFullScreen() uses the screen the window is on, so the
        // screen is set first, and the frame flag removes the title bar even if a window manager would add one.
        window.setWindowFlag(Qt::FramelessWindowHint,true);
        window.setScreen(kioskScreen);
        window.setGeometry(kioskScreen->geometry());
        window.showFullScreen();
        qInfo().noquote()<<QString("kiosk: frameless fullscreen on display %1 (%2), %3x%4")
            .arg(QGuiApplication::screens().indexOf(kioskScreen)).arg(kioskScreen->name())
            .arg(kioskScreen->geometry().width()).arg(kioskScreen->geometry().height());
    } else if(startWindowed)window.show();
    else if(shell.fullscreen())window.showFullScreen();else window.show();
    if(window.screen())qInfo()<<"native display refresh:"<<window.screen()->refreshRate()<<"Hz";
    if(parser.isSet("script")) {
        // Local verification: replay key presses and save framebuffer grabs at the window's real resolution, e.g.
        // --script "full,wait,LEFT,DOWN,grab=C:/tmp/a.png,quit". One token every 300 ms.
        auto tokens=std::make_shared<QStringList>(parser.value("script").split(','));
        const auto tap=[view](int key){view->replayKeyTap(key);};
        auto* timer=new QTimer(&app);timer->setInterval(300);
        QObject::connect(timer,&QTimer::timeout,&app,[&app,&window,&shell,view,tokens,timer,tap]{
            if(tokens->isEmpty()){timer->stop();return;}
            const QString token=tokens->takeFirst().trimmed();
            // XMB_INPUT_TRACE=1: the script is one of the two input sources the shell can hear in a scripted
            // run (the other is the keyboard). Logging the token as it is applied is what makes the trace
            // read as one decision list: source=script here, source=pad from the withheld live controller.
            if(inputTraceEnabled())qInfo().noquote()<<QString("input: trace source=script action=%1 accepted=1").arg(token);
            static const QHash<QString,int> keys{{"LEFT",Qt::Key_Left},{"RIGHT",Qt::Key_Right},{"UP",Qt::Key_Up},{"DOWN",Qt::Key_Down},
                {"ENTER",Qt::Key_Return},{"ESC",Qt::Key_Escape},{"PGUP",Qt::Key_PageUp},{"PGDN",Qt::Key_PageDown},{"BACKSPACE",Qt::Key_Backspace},
                {"PREV",Qt::Key_BracketLeft},{"NEXT",Qt::Key_BracketRight},{"SHUFFLE",Qt::Key_M},{"REPEAT",Qt::Key_R},
                {"ZOOMIN",Qt::Key_Plus},{"ZOOMOUT",Qt::Key_Minus},{"RESETVIEW",Qt::Key_0},{"ROTATELEFT",Qt::Key_Q},{"ROTATERIGHT",Qt::Key_E},{"REFRESH",Qt::Key_F5},
                {"F10",Qt::Key_F10},{"TRIANGLE",Qt::Key_T}}; // Triangle: the selected entry's options panel (T is its keyboard binding)
            if(token=="full")shell.setFullscreen(true);
            else if(token.startsWith("size=")) {
                const auto dimensions=token.mid(5).split('x');bool widthOk=false,heightOk=false;
                const int w=dimensions.value(0).toInt(&widthOk),h=dimensions.value(1).toInt(&heightOk);
                if(dimensions.size()==2 && widthOk && heightOk && w>=800 && h>=450 && w<=7680 && h<=4320) {
                    shell.setFullscreen(false);window.showNormal();window.resize(w,h);
                } else qWarning()<<"script: invalid size"<<token;
            }
            else if(token.startsWith("idle=")) {bool ok=false;const auto age=token.mid(5).toLongLong(&ok);if(ok && age>=0)shell.setIdleTimeOverride(age);else qWarning()<<"script: invalid idle age";}
            else if(token=="ps")view->replayPsTap();       // PS button tap: controller indicator
            else if(token=="pshold")view->replayPsHold();   // PS button hold: system menu
            else if(token.startsWith("NOTIFY=")) {
                // NOTIFY=<title>[|<detail>[|<icon>]] raises a notification through the same path a real event
                // uses, so a capture can pin it: the icon is a tex_notification_<name> suffix (default info)
                // or the full texture name. Commas are the script separator, so a title cannot contain one.
                const auto fields=token.mid(7).split('|');
                const QString title=fields.value(0).trimmed();
                if(title.isEmpty())qWarning()<<"script: invalid NOTIFY token (empty title)";
                else {
                    shell.notify(title,fields.value(1).trimmed(),fields.value(2).trimmed());
                    qInfo().noquote()<<"script: notify"<<title<<fields.value(1).trimmed()<<fields.value(2).trimmed();
                }
            }
            else if(token=="VOLUP" || token=="VOLDOWN" || token=="MUTE") {
                // The couch volume path: the same key a Windows media key becomes when Qt delivers it as a key
                // event, so the script drives View's real handler rather than a private shortcut.
                tap(token=="VOLUP"?Qt::Key_VolumeUp:token=="VOLDOWN"?Qt::Key_VolumeDown:Qt::Key_VolumeMute);
                qInfo().noquote()<<"script: volume key"<<token;
            }
            else if(token.startsWith("VOLSET=")) {
                bool ok=false;const int percent=token.mid(7).toInt(&ok);
                if(ok && percent>=0 && percent<=100){view->setMasterVolumePercent(percent);qInfo()<<"script: volume set"<<percent;}
                else qWarning()<<"script: invalid VOLSET token (whole percent, 0-100)";
            }
            else if(token.startsWith("VOLKEY=")) {
#ifdef Q_OS_WIN
                // The message forms a media key actually arrives in, posted to the XMB window so the native event
                // filter handles them exactly as it handles the real key: WM_APPCOMMAND (up|down|mute) and
                // WM_KEYDOWN with VK_VOLUME_* (vkup|vkdown|vkmute). Both are swallowed by the filter, which is
                // what keeps DefWindowProc from stepping the endpoint a second time.
                const QString what=token.mid(7);
                const int command=what=="up"?APPCOMMAND_VOLUME_UP:what=="down"?APPCOMMAND_VOLUME_DOWN:
                    what=="mute"?APPCOMMAND_VOLUME_MUTE:0;
                const WPARAM key=what=="vkup"?VK_VOLUME_UP:what=="vkdown"?VK_VOLUME_DOWN:
                    what=="vkmute"?VK_VOLUME_MUTE:0;
                if(!command && !key)qWarning()<<"script: invalid VOLKEY token (up|down|mute|vkup|vkdown|vkmute)";
                else {
                    const HWND handle=reinterpret_cast<HWND>(window.winId());
                    if(command)PostMessageW(handle,WM_APPCOMMAND,reinterpret_cast<WPARAM>(handle),MAKELPARAM(0,command));
                    else {PostMessageW(handle,WM_KEYDOWN,key,0);PostMessageW(handle,WM_KEYUP,key,0);}
                    qInfo().noquote()<<"script: volume message"<<what;
                }
#else
                qWarning()<<"script: VOLKEY is Windows only";
#endif
            }
            else if(token=="VOLRESTORE"){view->restoreMasterVolume();qInfo()<<"script: volume restore";}
            else if(token=="quit")app.quit();
            else if(token.startsWith("pause=")) {
                // Hold the script clock for N ms without consuming tokens, so real time (an idle Auto-Off, a media
                // timeout) can pass inside a scripted run, e.g. "pause=65000,grab=...,quit" with a low
                // power/idleMinutes. Bounded to ten minutes; the script resumes after it.
                bool ok=false;const auto milliseconds=token.mid(6).toLongLong(&ok);
                if(!ok || milliseconds<0 || milliseconds>600000)qWarning()<<"script: invalid pause token (whole milliseconds, 0-600000)";
                else {qInfo()<<"script: pause"<<milliseconds;timer->stop();QTimer::singleShot(int(milliseconds),timer,[timer]{timer->start();});}
            }
            else if(token.startsWith("grab=")) {
                const auto image=view->grabFramebuffer();
                qInfo().noquote()<<"script: grab"<<token.mid(5)<<image.size()<<(image.save(token.mid(5))?"saved":"FAILED");
                const QJsonObject state{{"category",shell.categories()[shell.category()].id},
                    {"categoryIndex",shell.category()},{"depth",shell.depth()},{"selection",shell.selection()},
                    {"width",image.width()},{"height",image.height()},{"clock",shell.clockText()},
                    {"entryId",shell.entries().isEmpty()?QString{}:shell.entries()[shell.selection()].id},
                    {"entryTitle",shell.entries().isEmpty()?QString{}:shell.entries()[shell.selection()].title},
                    {"entryCount",shell.entries().size()},{"mediaOpen",shell.mediaOpen()},{"mediaPaused",shell.mediaPaused()},
                    {"mediaProvider",shell.mediaProvider()},{"playMethod",shell.mediaPlayMethod()},{"mediaPosition",shell.mediaPosition()},{"mediaDuration",shell.mediaDuration()},{"idleMilliseconds",shell.idleMilliseconds()},{"screensaverMinutes",shell.screensaverMinutes()},
                    // Auto-Off (Phase 4.1): the display-off request this build made, and the last console display
                    // state Windows reported (-1 = never observed). The capture cannot power a monitor, so these are
                    // the sidecar's honest record; audit/SESSION-POWER.md.
                    {"displayOffRequested",xmb::host::displayOffRequested()},{"displayState",xmb::host::displayState()},
                    // Input integrity (audit/PS-BUTTON.md): in a scripted run, how many live pad samples the gate
                    // withheld and whether the gate was on. A deterministic capture reports a nonzero count while
                    // its frame and selection are still the script's own; a normal run runs the pad as before.
                    {"padEventsIgnored",double(xmb::pad::ignoredEvents())},{"inputScriptOnly",xmb::pad::scriptOnly()},
                    // Kiosk evidence (Phase 4.2): the window state at grab time. IsFullScreen is the widget's own
                    // state; the no-title-bar proof is the window rect/style read by the test's own Win32 query.
                    {"fullscreen",window.isFullScreen()},{"windowWidth",window.width()},{"windowHeight",window.height()},
                    {"notificationCount",shell.notificationCount()},{"notificationRemainingMs",double(shell.notificationRemainingMs())},
                    {"notificationTitle",shell.notification()?shell.notification()->title:QString{}},
                    {"notificationIcon",shell.notification()?shell.notification()->icon:QString{}},
                    // Sidecar subtitles and the audio track the player reports (audit/SUBTITLES.md). The
                    // cue number is 1-based and 0 when no cue covers the capture's position; audioTrack is
                    // 1-based and 0 when the backend names none, so both read as "none" without a sentinel.
                    {"subtitleFile",shell.subtitleFile()},{"subtitleLabel",shell.subtitleLabel()},
                    {"subtitleTrackCount",shell.subtitleTracks().size()},
                    {"subtitleCue",shell.subtitleCueNumber()},
                    {"subtitleCueText",shell.subtitleCueText().left(160)},
                    {"audioTrack",shell.activeAudioTrack()>=0?shell.activeAudioTrack()+1:0},
                    {"audioTrackCount",shell.audioTracks().size()},
                    {"audioTrackName",shell.audioTracks().value(shell.activeAudioTrack()).label},
                    // The page model and the in-XMB Jellyfin sign-in page (audit/JELLYFIN-NATIVE.md). pageKind
                    // names what the page under the cursor is; focusedRow repeats entryId as the explicit
                    // oracle T1 asked for; signInState is idle/signingIn/failed while the page is up and empty
                    // otherwise, and signInError is the client's own message. No credential is reported here:
                    // the password is not a field of this object and the user name is not either.
                    {"pageKind",shell.pageKind()},{"focusedRow",shell.focusedRowId()},
                    {"signInOpen",shell.jellyfinSignInOpen()},
                    {"signInState",shell.jellyfinSignInState()},
                    {"signInError",shell.jellyfinSignInError()},
                    // The widget class of the application's active modal, or "" when none is up. This is the
                    // oracle for "no modal widget blocks pollController": the sign-in page and its keyboard
                    // capture with an empty value, where the retired QDialog reported "JellyfinSignIn".
                    {"modalWidget",QApplication::activeModalWidget()?
                        QString::fromLatin1(QApplication::activeModalWidget()->metaObject()->className()):QString{}}};
                QFile stateFile(token.mid(5)+".json");const auto bytes=QJsonDocument(state).toJson();
                if(!stateFile.open(QIODevice::WriteOnly) || stateFile.write(bytes)!=bytes.size())qWarning()<<"script: state save FAILED"<<stateFile.errorString();
                qInfo()<<"script: state"<<state;
            } else if(token.startsWith("burst=")) {
                // Consecutive frames as fast as the window renders, kept in memory and saved afterwards as
                // prefix_NN_<ms since the trigger>.png. Grammar:
                //   burst=<prefix>[@<key|ps|pshold>[@<frames>[@<intervalMs>]]]
                // Defaults are 12 frames at 12 ms. The trigger is pressed on the same tick as the onset grab and
                // an immediate grab is recorded BEFORE the timer starts, because a bare 12 ms timer loses 20-160
                // ms to grabFramebuffer before its first frame and the transition onset became unobservable
                // (transition harness, 2026-10-28). ps/pshold reuse the PS-button script verbs so a burst can
                // start on a surface the plain key map cannot press.
                const auto parts=token.mid(6).split('@');const QString prefix=parts[0];
                int count=12;int intervalMs=12;
                if(parts.size()>2 && parts[2].toInt()>0)count=std::clamp(parts[2].toInt(),1,600);
                if(parts.size()>3 && parts[3].toInt()>0)intervalMs=std::clamp(parts[3].toInt(),1,1000);
                auto started=std::make_shared<QElapsedTimer>();started->start();
                if(parts.size()>1) {
                    if(keys.contains(parts[1]))tap(keys[parts[1]]);
                    else if(parts[1]=="ps")view->replayPsTap();
                    else if(parts[1]=="pshold")view->replayPsHold();
                }
                auto frames=std::make_shared<QVector<QPair<qint64,QImage>>>();
                const auto save=[frames,prefix]{for(int i=0;i<frames->size();++i)
                        (*frames)[i].second.save(prefix+QString("_%1_%2ms.png").arg(i,2,10,QChar(48)).arg((*frames)[i].first,4,10,QChar(48)));};
                frames->push_back({started->elapsed(),view->grabFramebuffer()});
                if(frames->size()>=count){save();}
                else {
                    auto* burst=new QTimer(&app);burst->setTimerType(Qt::PreciseTimer);burst->setInterval(intervalMs);
                    QObject::connect(burst,&QTimer::timeout,&app,[view,burst,frames,prefix,started,count,save]{
                        frames->push_back({started->elapsed(),view->grabFramebuffer()});
                        if(frames->size()<count)return;
                        burst->stop();burst->deleteLater();save();
                    });
                    burst->start();
                }
            } else if(token.startsWith("type=")) {
                for(const QChar c:token.mid(5)){QKeyEvent event(QEvent::KeyPress,0,Qt::NoModifier,QString(c));QCoreApplication::sendEvent(view,&event);}
            } else if(keys.contains(token)) {
                tap(keys[token]);
            }
        });
        QTimer::singleShot(2500,timer,[timer]{timer->start();});
    }
    // XMB_INPUT_TRACE=1 also records real keyboard input. The filter must outlive app.exec(), so it owns the
    // whole run once installed; the script's own replayKeyTap() calls the view directly and is logged as
    // source=script by the token loop instead, so the two sources never double-report one key.
    std::unique_ptr<KeyboardTrace> keyboardTrace;
    if(inputTraceEnabled()) {
        keyboardTrace=std::make_unique<KeyboardTrace>();
        app.installEventFilter(keyboardTrace.get());
    }
    return app.exec();
}
