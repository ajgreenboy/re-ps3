#include "shell.h"
#include "settings_menu.h"
#include "assets.h"
#include "host.h"
#include "p3t.h"
#include "xmbml_view.h"
#include "rco_objects.h"
#include <cmath>
#include <memory>
#include <iterator>
#include <QTimeZone>
#include <QDateTime>
#include <QMetaType>
#include <QImageReader>
#include <QFileInfo>
#include <QFile>
#include <QSysInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace xmb {
namespace {
struct Choice {QString title;QVariant value;};
// The console's Date Format and Time Format options (sysconf_plugin msg_datetime_fmt_* and msg_time_fmt_*).
// The short forms feed the information-panel clock, the full forms the Date and Time screen. Fields are
// zero-padded like the firmware's own option labels and its reference frame (25/10); the hour is not padded,
// which is the US clock convention ("1:05 PM", not "01:05 PM").
QString clockDate(int format) {return QStringList{"dd/MM","MM/dd","yyyy/MM/dd"}.value(format,"MM/dd");}
QString clockFullDate(int format) {return QStringList{"dd/MM/yyyy","MM/dd/yyyy","yyyy/MM/dd"}.value(format,"MM/dd/yyyy");}
QString clockTime(int format) {return format?"h:mm AP":"HH:mm";}
QString clockFullTime(int format) {return format?"h:mm:ss AP":"HH:mm:ss";}
QVector<Choice> choices(const QString& key,Assets& assets) {
    const auto text=[&](const QString& id,const QString& fallback){return assets.text("sysconf_plugin",id,fallback);};
    if(key=="appearance/colour")return {{"Original",0},{"Blue",1},{"Purple",2},{"Red",3},{"Green",4},{"Silver",5}};
    // The theme chosen with Select Theme joins this list in openChoices (it is read from the user's .p3t).
    if(key=="appearance/theme")return {{text("msg_theme_default","Original"),QString{}}};
    if(key=="appearance/background")return {{text("msg_theme_default","Original"),0},{text("msg_theme_classic","Classic"),1},{"Wallpaper",2}};
    if(key=="appearance/font") {
        QVector<Choice> result;const auto families=assets.fontFamilies();
        for(int i=0;i<families.size();++i)result.push_back({i==0?text("msg_theme_default","Original"):families[i],i});
        return result;
    }
    if(key=="clock/dateFormat")return {{text("msg_datetime_fmt_dmy","DD/MM/YYYY"),0},{text("msg_datetime_fmt_mdy","MM/DD/YYYY"),1},{text("msg_datetime_fmt_ymd","YYYY/MM/DD"),2}};
    if(key=="clock/hourFormat")return {{text("msg_time_fmt_24","24-Hour Clock"),0},{text("msg_time_fmt_12","12-Hour Clock"),1}};
    if(key=="clock/timeZone") {
        static const QVector<Choice> zones=[] {
            QVector<Choice> result{{"System",QString("System")}};
            for(const auto& id:QTimeZone::availableTimeZoneIds())result.push_back({QString::fromUtf8(id),QString::fromUtf8(id)});
            return result;
        }();
        return zones;
    }
    if(key=="audio/volume" || key=="host/volume") {QVector<Choice> result;for(int i=0;i<=100;i+=10)result.push_back({QString::number(i)+"%",i});return result;}
    if(key=="photo/interval")return {{"5 seconds",5000},{"10 seconds",10000},{"15 seconds",15000},{"30 seconds",30000}};
    if(key=="photo/fit")return {{"Fit",0},{"Fill",1}};
    if(key=="media/repeatMode")return {{"Off",0},{"Repeat One",1},{"Repeat All",2}};
    if(key=="display/screensaverMinutes")return {{"After 5 Minutes",5},{"After 10 Minutes",10},{"After 20 Minutes",20},{"After 30 Minutes",30},{"Do Not Use",0}};
    if(key=="power/idleMinutes")return {{"Off",0},{"15 minutes",15},{"30 minutes",30},{"1 hour",60},{"2 hours",120}};
    return {{"Off",false},{"On",true}};
}
// One row of a settings list page. The row class is this build's: the glass settings bullet
// (qgl/icontex "23n"), which the view draws at the firmware's 18 px/39 px settings-row size. The
// firmware's brass wrench disc (tex_sett) is the same footprint but a different size on screen
// (40/56 through the item-glyph path), so D21's texture change needs the view.cpp size hunk and is
// not part of a row-set change (audit/SETTINGS-FIRMWARE.md D21).
Entry option(QString id,QString title,QString detail,QString setting,Action action=Action::SettingChoices,QString value={}) {
    Entry entry{std::move(id),std::move(title),std::move(detail),"qgl/icontex","23n",action,std::move(value)};
    entry.setting=std::move(setting);entry.themeIcon="icon_setting_item";return entry;
}
// --- The firmware row sets (Phase 11.1) -----------------------------------------------------------
// audit/SETTINGS-FIRMWARE.md 5.2-5.16 lists each page's rows from sysconf_plugin's own sub-pages and
// chart pages. A row is described once, by the resource keys the firmware uses for its label and its
// explanation, plus what this build really does with it. There is deliberately no "msg_"+id synthesis
// (7.4): a row whose label key is not named here has no label, and the row is not invented.
struct RowSpec {
    const char* id=nullptr;          // the row's stable native id (page dumps and logs)
    const char* titleKey=nullptr;    // sysconf_plugin msg_*
    const char* infoKey=nullptr;     // the row's *_explanation, when the RCO has one
    Action action=Action::Unavailable;
    const char* setting=nullptr;     // the Preferences key a SettingChoices/EditName row writes
    const char* value=nullptr;       // the Information payload (host:network, host:display, ...)
    const char* literalTitle=nullptr;   // last-resort label, and the whole label of a native-only row
    const char* literalDetail=nullptr;  // native-only explanation (never used for a firmware row)
};
Entry rowEntry(const RowSpec& spec,Assets& assets) {
    const auto text=[&assets](const char* key){return key?assets.text("sysconf_plugin",QString::fromLatin1(key)):QString{};};
    const auto title=text(spec.titleKey);
    const auto detail=text(spec.infoKey);
    const auto literalTitle=spec.literalTitle?QString::fromLatin1(spec.literalTitle):QString{};
    const auto literalDetail=spec.literalDetail?QString::fromLatin1(spec.literalDetail):QString{};
    Entry entry=option(QString::fromLatin1(spec.id),title.isEmpty()?literalTitle:title,
        detail.isEmpty()?literalDetail:detail,spec.setting?QString::fromLatin1(spec.setting):QString{},
        spec.action,spec.value?QString::fromLatin1(spec.value):QString{});
    // Where the label came from, so the entry dump says which rows are the firmware's and which are this
    // build's own (Entry::source, audit/MENUS-XMBML.md 4).
    if(!title.isEmpty())entry.source=QStringLiteral("rco");
    else entry.source=literalTitle.isEmpty()?QString{}:QStringLiteral("builtin");
    return entry;
}
// The one-level settings pages, in the order the evidence gives: section 5.2-5.16 for the rows, and the
// 2026-10-28 capture batch (section 9.2) for the measured top of Video, Music, Security and Remote Play.
// A row this build can act on keeps its native action and Preferences key; a firmware row it cannot act
// on is listed as Action::Unavailable with the RCO's own explanation, which is the "list-unavailable"
// disposition (MENUS-XMBML 4): the page is the console's row set, not a subset of it.
const QVector<RowSpec>* firmwareRows(const QString& id) {
    static const QVector<RowSpec> theme{
        {"theme","msg_theme","msg_theme_explanation",Action::SettingChoices,"appearance/theme",nullptr,"Theme"},
        {"color","msg_color","msg_color_explanation",Action::SettingChoices,"appearance/colour",nullptr,"Color"},
        {"background","msg_background","msg_background_explanation",Action::SettingChoices,"appearance/background",nullptr,"Background"},
        {"font","msg_font","msg_font_explanation",Action::SettingChoices,"appearance/font",nullptr,"Font"}};
    static const QVector<RowSpec> date{
        {"datetime","msg_datetime","msg_datetime_explanation",Action::Information,nullptr,nullptr,"Date and Time"},
        {"autodate","msg_automatic_datetime_setting","msg_automatic_datetime_setting_explanation",Action::Unavailable,nullptr,nullptr,"Set Automatically"},
        {"date_fmt","msg_date_fmt","msg_date_fmt_explanation",Action::SettingChoices,"clock/dateFormat",nullptr,"Date Format"},
        {"time_fmt","msg_time_fmt","msg_time_fmt_explanation",Action::SettingChoices,"clock/hourFormat",nullptr,"Time Format"},
        {"timezone","msg_timezone","msg_timezone_explanation",Action::SettingChoices,"clock/timeZone",nullptr,"Time Zone"},
        {"summertime","msg_summertime","msg_summertime_explanation",Action::Unavailable,nullptr,nullptr,"Daylight Saving"}};
    static const QVector<RowSpec> system{
        {"console_lang","msg_console_lang","msg_console_lang_explanation",Action::Unavailable,nullptr,nullptr,"System Language"},
        {"name","msg_ps3_name","msg_ps3_name_explanation",Action::EditName,"system/name",nullptr,"System Name"},
        {"disc_utility","msg_disc_utility","msg_disc_utility_explanation",Action::Unavailable,nullptr,nullptr,"Format Utility"},
        {"backup_utility","msg_backup_utility","msg_backup_utility_explanation",Action::Unavailable,nullptr,nullptr,"Backup Utility"},
        {"init_setting","msg_init_setting","msg_init_setting_explanation",Action::Unavailable,nullptr,nullptr,"Restore Default Settings"},
        {"init_console","msg_init_console","msg_init_console_explanation",Action::Unavailable,nullptr,nullptr,"Restore PS3 System"},
        {"ex_os_install","msg_ex_os_install","msg_ex_os_install_explanation",Action::Unavailable,nullptr,nullptr,"Install Other OS"},
        {"information","msg_console_info","msg_console_info_explanation",Action::Information,nullptr,nullptr,"System Information"},
        {"aboutps3","msg_aboutps3","msg_aboutps3_explanation",Action::Unavailable,nullptr,nullptr,"About PlayStation 3"}};
    static const QVector<RowSpec> sound{
        {"sound_output_setting","msg_sound_output_setting","msg_sound_output_setting_explanation",Action::Information,nullptr,"host:audio","Audio Output Settings"},
        {"audio_multi_output","msg_audio_multi_output","msg_audio_multi_output_explanation",Action::Unavailable,nullptr,nullptr,"Audio Multi-Output"},
        {"keytone","msg_sound_key","msg_sound_key_explanation",Action::SettingChoices,"audio/keyTone",nullptr,"Key Tone"}};
    static const QVector<RowSpec> display{
        {"display_output_setting","msg_display_output_setting","msg_display_output_setting_explanation",Action::Information,nullptr,"host:display","Video Output Settings"},
        {"screensaver","msg_screensaver","msg_screensaver_explanation",Action::SettingChoices,"display/screensaverMinutes",nullptr,"Screensaver"},
        {"cross_color_filter","msg_cross_color_filter","msg_cross_color_filter_explanation",Action::Unavailable,nullptr,nullptr,"Cross Color Reduction Filter"},
        {"rgb_full_range","msg_rgb_full_range_hdmi","msg_rgb_full_range_hdmi_explanation",Action::Unavailable,nullptr,nullptr,"RGB Full Range (HDMI)"},
        {"super_white","msg_ycbcr_super_white_hdmi","msg_ycbcr_super_white_hdmi_explanation",Action::Unavailable,nullptr,nullptr,"Y Pb/Cb Pr/Cr Super-White (HDMI)"},
        {"deep_color","msg_deep_color_output_hdmi","msg_deep_color_output_hdmi_explanation",Action::Unavailable,nullptr,nullptr,"Deep Color Output (HDMI)"}};
    static const QVector<RowSpec> security{
        {"pw_change","msg_pw_change","msg_pw_change_explanation",Action::Unavailable,nullptr,nullptr,"Change Password"},
        {"bd_pc","msg_bd_pc","msg_bd_pc_explanation",Action::Unavailable,nullptr,nullptr,"BD - Parental Control"},
        {"bd_dvd_pcregion","msg_bd_dvd_use_pcregion","msg_bd_dvd_use_pcregion_explanation",Action::Unavailable,nullptr,nullptr,"BD/DVD - Parental Control Region Code"},
        {"dvd_pc","msg_dvd_pc","msg_dvd_pc_explanation",Action::Unavailable,nullptr,nullptr,"DVD - Parental Control"},
        {"gameetc_pc","msg_gameetc_pc","msg_gameetc_pc_explanation",Action::Unavailable,nullptr,nullptr,"Parental Control"},
        {"pc_browser","msg_pc_browser","msg_pc_browser_explanation",Action::Unavailable,nullptr,nullptr,"Internet Browser Start Control"}};
    // Remote Play's five rows are the measured ones (capture batch 2026-10-28, SETTINGS 9.2); the
    // registration/list/custom-SSID rows open their own pages in the firmware and have no native action.
    static const QVector<RowSpec> remote{
        {"device_registration","msg_device_registration","msg_device_registration_explanation",Action::Unavailable,nullptr,nullptr,"Register Device"},
        {"device_registration_list","msg_device_registration_list","msg_device_registration_list_explanation",Action::Unavailable,nullptr,nullptr,"Registered Device List"},
        {"custom_connection_setting","msg_custom_connection_setting","msg_custom_connection_setting_explanation",Action::Unavailable,nullptr,nullptr,"Custom Connection Settings"},
        {"remote_start","msg_remote_start","msg_remote_start_explanation",Action::Unavailable,nullptr,nullptr,"Remote Start"},
        {"audio_output_device","msg_audio_output_device","msg_audio_output_device_explanation",Action::Unavailable,nullptr,nullptr,"Audio Output Device"}};
    // Video and Music: the measured top of each page (capture batch 2026-10-28, SETTINGS 9.2). Video's
    // rows below the seventh (the remaining DVD/HDD/upscaler rows) are still unmeasured, so they are not
    // invented here; Music stops at five rows in the still with no sixth row before the frame edge.
    static const QVector<RowSpec> video{
        {"bd_menu_lang","msg_bd_menu_lang","msg_bd_menu_lang_explanation",Action::Unavailable,nullptr,nullptr,"BD - Menu Language"},
        {"bd_audio_lang","msg_bd_audio_lang","msg_bd_audio_lang_explanation",Action::Unavailable,nullptr,nullptr,"BD - Audio Language"},
        {"bd_subtitle_lang","msg_bd_subtitle_lang","msg_bd_subtitle_lang_explanation",Action::Unavailable,nullptr,nullptr,"BD - Subtitle Language"},
        {"bd_internet_connection","msg_bd_internet_connection","msg_bd_internet_connection_explanation",Action::Unavailable,nullptr,nullptr,"BD - Internet Connection"},
        {"dvd_menu_lang","msg_dvd_menu_lang","msg_dvd_menu_lang_explanation",Action::Unavailable,nullptr,nullptr,"DVD - Menu Language"},
        {"dvd_audio_lang","msg_dvd_audio_lang","msg_dvd_audio_lang_explanation",Action::Unavailable,nullptr,nullptr,"DVD - Audio Language"},
        {"dvd_subtitle_lang","msg_dvd_subtitle_lang","msg_dvd_subtitle_lang_explanation",Action::Unavailable,nullptr,nullptr,"DVD - Subtitle Language"}};
    static const QVector<RowSpec> music{
        {"import_musiccd","msg_import_musiccd","msg_import_musiccd_explanation",Action::Unavailable,nullptr,nullptr,"Audio CD Import"},
        {"crossfade_play","msg_crossfade_play","msg_crossfade_play_explanation",Action::Unavailable,nullptr,nullptr,"Crossfade Playback"},
        {"output_frequency","msg_output_frequency","msg_output_frequency_explanation",Action::Unavailable,nullptr,nullptr,"Output Frequency"},
        {"bitmapping","msg_bitmapping_sacd","msg_bitmapping_sacd_explanation",Action::Unavailable,nullptr,nullptr,"Bitmapping"},
        {"dnormal","msg_music_dnormal","msg_music_dnormal_explanation",Action::Unavailable,nullptr,nullptr,"Dynamic Normalizer"}};
    static const QVector<RowSpec> print{
        {"printer_selection","msg_printer_selection","msg_printer_selection_explanation",Action::Unavailable,nullptr,nullptr,"Printer Selection"},
        {"printer_manager","msg_printer_manager","msg_printer_manager_explanation",Action::Unavailable,nullptr,nullptr,"Printer Manager"}};
    static const QVector<RowSpec> power{
        {"idle","msg_system_auto_power_off","msg_system_auto_power_off_explanation",Action::SettingChoices,"power/idleMinutes",nullptr,"System Auto-Off"},
        {"controller","msg_controller_auto_power_off","msg_controller_auto_power_off_explanation",Action::Unavailable,nullptr,nullptr,"Controller Auto-Off"}};
    static const QVector<RowSpec> chat{
        {"av_chat_start","msg_av_chat_start","msg_av_chat_start_explanation",Action::Unavailable,nullptr,nullptr,"Voice/Video Chat Start"},
        {"audio_device_setting","msg_audio_device_setting","msg_audio_device_setting_explanation",Action::Unavailable,nullptr,nullptr,"Audio Device Settings"},
        {"camera_device_setting","msg_camera_device_setting","msg_camera_device_setting_explanation",Action::Unavailable,nullptr,nullptr,"Camera Device Settings"}};
    if(id=="theme")return &theme;
    if(id=="date")return &date;
    if(id=="system")return &system;
    if(id=="sound")return &sound;
    if(id=="display")return &display;
    if(id=="security")return &security;
    if(id=="remote")return &remote;
    if(id=="print")return &print;
    if(id=="power")return &power;
    if(id=="chat")return &chat;
    if(id=="video")return &video;
    if(id=="music")return &music;
    return nullptr;
}
// Rows the firmware's page does not have. They stay only where this build's own feature would otherwise
// lose its only surface; each one is marked in the dump and in SETTINGS-FIRMWARE.md section 9.
const QVector<RowSpec>* nativeOnlyRows(const QString& id) {
    // D11: the firmware folds theme and wallpaper selection into the Theme and Background choice pages,
    // which this build cannot bind yet (the choice page class is Phase 11.3), so the two pickers stay as
    // native-only rows at the end of the page. Removing them would delete the only way to load a .p3t
    // theme or a wallpaper.
    static const QVector<RowSpec> theme{
        {"themefile",nullptr,nullptr,Action::Theme,nullptr,nullptr,"Select Theme",
            "Opens a PS3 theme file (.p3t): its background and icons replace the system's."},
        {"wallpaper",nullptr,nullptr,Action::Wallpaper,nullptr,nullptr,"Select Wallpaper",
            "Selects a local image for the XMB background."}};
    // D6/D7: the firmware's Video and Music pages are BD/DVD and audio-CD rows (listed above from the
    // measured batch); neither drive nor decoder exists in this build, so every one of them is an
    // explained refusal. These three are this build's own media settings and stay as native-only rows
    // at the end of the page rather than being dropped - they are the page's only working rows.
    static const QVector<RowSpec> media{
        {"repeat",nullptr,nullptr,Action::SettingChoices,"media/repeatMode",nullptr,"Repeat",
            "Stops at the end, repeats one file, or repeats the current folder queue."},
        {"shuffle",nullptr,nullptr,Action::SettingChoices,"media/shuffle",nullptr,"Shuffle",
            "Plays every file in a random order before starting another cycle."},
        {"volume",nullptr,nullptr,Action::SettingChoices,"audio/volume",nullptr,"Volume",
            "Sets native media playback volume."}};
    if(id=="theme")return &theme;
    if(id=="music" || id=="video")return &media;
    return nullptr;
}
// A page's rows, plus what the definition said and what was left out: one record the app logs, dumps and
// the audit can cite.
struct PageRows {
    QString id,title;
    QVector<Entry> entries;
    QStringList notes;      // "key: reason", one per refused row
    QStringList nativeOnly; // ids of the rows in this build and not in the firmware's page
    QJsonArray firmware;    // the definition's own rows (or the RCO sub-page set), row for row
};
// --- The three two-level pages: Game, Accessory, Network -------------------------------------------
// audit/SETTINGS-FIRMWARE.md 1.1/5.1/5.9/5.15: in the definition each of the three is a child=segment
// row whose page lists the rows of seg_<name> -> seg_*_fixed_upper|middle|lower (+ the USB-headset,
// keyboard-input and DLNA groups). resolveSegmentView() reads that from category_sysconf.xml; this table
// only says what this build does with each firmware row. A row with no binding is a listed refusal with
// the RCO's own explanation, which is what the console's conditional rows become on a PC.
PageRows segmentRows(const QString& id,Assets& assets) {
    static const QHash<QString,QString> views{{"game","seg_game"},{"device","seg_device"},{"network","seg_network"}};
    static const QHash<QString,QString> titles{{"game","msg_game_setting"},{"device","msg_pd_setting"},{"network","msg_net_setting"}};
    struct Binding { Action action; const char* setting; const char* value; };
    // The one firmware row of these pages this build can really perform: the network status list, which
    // is the host's own report (audit/SETTINGS-FIRMWARE.md 5.15 lists it first in the definition).
    static const QHash<QString,Binding> bindings{{"net_netlist",{Action::Information,nullptr,"host:network"}}};
    // Rows the definition binds in a group this build cannot reach: the wireless-headset row only exists
    // when a Sony wireless stereo headset is connected, and the kana group's keybind row only exists for
    // Japanese input. Both would be permanently dead rows, so they are left out and the reason logged.
    static const QHash<QString,QString> skipped{
        {"pd_usbhdset_setting","the row only exists when a Sony wireless stereo headset is connected"},
        {"pd_keybind","Japanese kana input only"}};
    PageRows data;data.id=id;
    const auto title=assets.text("explore_category_sysconf",titles.value(id));
    data.title=title.isEmpty()?id:title;
    const bool verbose=qEnvironmentVariableIsSet("XMB_XMBML_LOG") && qEnvironmentVariable("XMB_XMBML_LOG")!=QStringLiteral("0");
    XmbmlResolveOptions options;
    options.flashRoot=assets.root();
    // The page rows' labels are sysconf_plugin's (1985 entries); a key the category RCO also carries is
    // resolved from sysconf_plugin first, because that is the table the settings pages' labels live in.
    options.container=QStringLiteral("sysconf_plugin");
    const auto column=resolveSegmentView(QStringLiteral("category_sysconf"),views.value(id),
        [&assets](const QString& container,const QString& key){
            const auto value=assets.text(container,key);
            if(!value.isEmpty())return value;
            return assets.text("explore_category_sysconf",key);
        },options);
    if(!column.ok()) {
        qWarning().noquote()<<"settings:"<<id<<"segment page cannot be resolved:"<<column.error<<column.path;
        data.notes<<QStringLiteral("definition: ")+column.error;
        return data;
    }
    QSet<QString> seen;
    for(const auto& row:column.rows) {
        const auto key=row.itemKey.isEmpty()?row.queryKey:row.itemKey;
        if(const auto why=skipped.constFind(key);why!=skipped.cend()) {
            data.notes<<key+QStringLiteral(": ")+*why;
            if(verbose)qInfo().noquote()<<"settings:"<<id<<"skip"<<key<<"("<<*why<<")";
            continue;
        }
        if(seen.contains(key)) {
            // pd_keyboard_input is bound by both mutually exclusive keyboard-input groups; one row.
            data.notes<<key+QStringLiteral(": bound by two mutually exclusive groups, listed once");
            continue;
        }
        seen.insert(key);
        if(row.title.isEmpty()) {
            data.notes<<key+QStringLiteral(": no title in sysconf_plugin, left out");
            qWarning().noquote()<<"settings:"<<id<<"row"<<key<<"has no title";
            continue;
        }
        const auto binding=bindings.constFind(key);
        Entry entry=option(row.id,row.title,row.detail,
            binding==bindings.cend()?QString{}:QString::fromLatin1(binding->setting),
            binding==bindings.cend()?Action::Unavailable:binding->action,
            binding==bindings.cend()?QString{}:QString::fromLatin1(binding->value));
        entry.source=fieldSourceName(row.titleSource);
        data.firmware.push_back(QJsonObject{{"id",row.id},{"key",key},{"group",row.queryKey},
            {"title",row.title},{"titleRsc",row.titleRsc},{"infoRsc",row.infoRsc},{"iconRsc",row.iconRsc},
            {"action",int(entry.action)}});
        if(verbose)qInfo().noquote()<<"settings:"<<id<<"row"<<entry.id<<"group="+row.queryKey
            <<"title=\""+entry.title+"\""<<"icon="+row.iconRsc<<"action="+QString::number(int(entry.action));
        data.entries.push_back(std::move(entry));
    }
    qInfo().noquote()<<QStringLiteral("settings: %1 segment page resolved %2 rows from %3 "
        "(groups %4, sha256 %5): listed %6, left out %7, rco titles %8")
        .arg(id).arg(int(data.entries.size())).arg(QFileInfo(column.path).fileName()).arg(column.queries)
        .arg(column.sha256.left(12)).arg(int(data.entries.size())).arg(data.notes.size()).arg(column.rcoRows);
    return data;
}
PageRows pageRows(const QString& id,Assets& assets,const QString& dateTimeInfo) {
    if(id=="game"||id=="device"||id=="network")return segmentRows(id,assets);
    PageRows data;data.id=id;
    static const QHash<QString,QString> pageTitles{
        {"theme","msg_theme_settings"},{"date","msg_datetime_setting"},{"system","msg_console_setting"},
        {"sound","msg_sound_setting"},{"display","msg_display_setting"},{"security","msg_security_setting"},
        {"remote","msg_remoteplay_setting"},{"print","msg_printer_settings"},{"power","msg_power_saving_setting"},
        {"chat","msg_chat_setting"},{"video","msg_video_setting"},{"music","msg_music_setting"}};
    const auto* rows=firmwareRows(id);
    const auto* extra=nativeOnlyRows(id);
    // A page may carry only native-only rows (none does today) or only firmware rows; both are pages.
    if((!rows && !extra) || !pageTitles.contains(id))return data;
    const auto title=assets.text("sysconf_plugin",pageTitles.value(id));
    data.title=title.isEmpty()?id:title;
    if(rows)for(const auto& spec:*rows) {
        Entry entry=rowEntry(spec,assets);
        // The Date and Time row's own page carries the live clock readout (the firmware opens a set page
        // there; this build reports the host clock it follows - SETTINGS 5.7).
        if(id=="date" && entry.id=="datetime")entry.value=dateTimeInfo;
        data.firmware.push_back(QJsonObject{{"id",QString::fromLatin1(spec.id)},
            {"titleRsc",spec.titleKey?QString::fromLatin1(spec.titleKey):QString{}},
            {"infoRsc",spec.infoKey?QString::fromLatin1(spec.infoKey):QString{}},
            {"action",int(entry.action)}});
        data.entries.push_back(std::move(entry));
    }
    if(extra)for(const auto& spec:*extra) {
        Entry entry=rowEntry(spec,assets);
        data.nativeOnly<<entry.id;
        data.entries.push_back(std::move(entry));
    }
    qInfo().noquote()<<QStringLiteral("settings: %1 page from the RCO row set: %2 rows, %3 native-only")
        .arg(id).arg(int(data.entries.size())).arg(data.nativeOnly.size());
    return data;
}
// === Phase 11.3: the choice list a settings row opens (audit/SETTINGS-FIRMWARE.md 3.1, 9.3a) =======
// The measured band holds 27 items at the 40 px pitch over 1080 px (SETTINGS 9.1/U2); a longer list has
// to scroll. Items that fit need no Scroll object, and the RCO ships one only on some pages.
constexpr int kBandRows=27;
// The firmware's choice pages are page class B: Page{Plane{BG0, BG1, LowerMenu(MList) -> MItem...}}.
// Every MItem carries its own label as a text reference, so the labels and their order are read from
// sysconf_plugin.rco's own bytes at run time, never typed in. What the RCO does not carry is the
// current-value mark (MItems have no children: SETTINGS 3.1 and U4, and the frames show no check/dot),
// so isCurrent is computed against the persisted preference by the caller.
//
// One reader per process, bound to the first Assets it sees. Constructing it does no I/O; the first
// container() call reads and parses sysconf_plugin.rco once, and later calls are cached
// (audit/RCO-OBJECT-MODEL.md). The application has one Assets for its lifetime; a second instance would
// be a wiring error, so it is logged rather than silently served from the wrong firmware tree.
RcoObjects& choiceObjects(Assets& assets) {
    static Assets* owner=nullptr;
    static std::unique_ptr<RcoObjects> reader;
    if(!reader){owner=&assets;reader=std::make_unique<RcoObjects>(assets);}
    else if(owner!=&assets)qWarning()<<"settings: the choice reader is bound to another Assets instance";
    return *reader;
}
// How a page's authored item maps to the value this build stores. The RCO's item order is the screen
// order (verified on screensaver-list-rest.png and timeformat-list-rest.png); the native values are the
// ones choices() already writes, so a view pass that draws these items commits exactly what it does now.
enum class ChoiceValues {
    Index,            // the item's position in the page (inert rows: not a preference value)
    Bool,             // Off/On -> false/true
    DateFormat,       // YYYY/MM/DD -> 2, DD/MM/YYYY -> 0, MM/DD/YYYY -> 1 (choices()'s values)
    HourFormat,       // 12-Hour Clock -> 1, 24-Hour Clock -> 0 (choices()'s values)
    ScreenMinutes,    // 5, 10, 20, 30, 0 (choices()'s values; Do Not Use is 0)
    Zone,             // the firmware zone token clock/timeZone stores (see clockDateTime)
};
// One row whose firmware counterpart is a choice page. `rcoPage` empty means no page object in
// sysconf_plugin.rco: that is a mismatch stated in the note, not a list to invent.
struct ChoiceBinding {
    const char* page=nullptr;
    const char* row=nullptr;
    const char* rcoPage=nullptr;
    const char* note=nullptr;
    ChoiceValues values=ChoiceValues::Index;
};
const QVector<ChoiceBinding>& choiceBindings() {
    // RCO page objects and item counts: SETTINGS 3.3; row ids: the pages in 10.4 and 10.3. A page that
    // exists but is empty (its Page has no Plane/MList items) is the run-time-built case of 9.3f.
    static const QVector<ChoiceBinding> table{
        // Game Settings (seg_game; every row is an explained refusal in this build)
        {"game","game_psp_emu_viewmode","page_game_config_pspemu_viewmode",nullptr,ChoiceValues::Index},
        {"game","game_psp_emu_3d",nullptr,"no page object for the 3D Display row in sysconf_plugin.rco",ChoiceValues::Index},
        {"game","game_psp_emu_adhoc_ch","page_game_config_pspemu_adhoc_mode_channel",nullptr,ChoiceValues::Index},
        {"game","game_psp_emu_adhoc_mode","page_game_config_pspemu_adhoc_mode_wlan",nullptr,ChoiceValues::Index},
        {"game","game_ps_emu_upcon","page_game_config_ps_ps2_upconvert",nullptr,ChoiceValues::Index},
        {"game","game_ps_emu_smooth","page_game_config_ps_ps2_smoothing",nullptr,ChoiceValues::Index},
        // Video Settings (the seven measured rows of 9.2; the BD/DVD language pages are 18 items each)
        {"video","bd_menu_lang","page_bdvd_config_bd_menu_lang",nullptr,ChoiceValues::Index},
        {"video","bd_audio_lang","page_bdvd_config_bd_voice_lang",nullptr,ChoiceValues::Index},
        {"video","bd_subtitle_lang","page_bdvd_config_bd_subtitles_lang",nullptr,ChoiceValues::Index},
        {"video","bd_internet_connection","page_bdvd_config_internet_connect",nullptr,ChoiceValues::Bool},
        {"video","dvd_menu_lang","page_bdvd_config_dvd_menu_lang",nullptr,ChoiceValues::Index},
        {"video","dvd_audio_lang","page_bdvd_config_dvd_voice_lang",nullptr,ChoiceValues::Index},
        {"video","dvd_subtitle_lang","page_bdvd_config_dvd_subtitles_lang",nullptr,ChoiceValues::Index},
        // Music Settings (9.2: five rows; the codec is the Audio CD Import row's value)
        {"music","import_musiccd","page_music_config_codec_select",
            "the frame's value for this row is the codec (AAC 128 kbps); the page names the codec only",ChoiceValues::Index},
        {"music","crossfade_play","page_music_config_crossfade_play",nullptr,ChoiceValues::Index},
        {"music","output_frequency","page_music_config_output_frequency",nullptr,ChoiceValues::Index},
        {"music","bitmapping","page_music_config_bitmapping",nullptr,ChoiceValues::Index},
        {"music","dnormal","page_music_config_dnormal",nullptr,ChoiceValues::Bool},
        // Chat Settings
        {"chat","av_chat_start","page_chat_config_when_chat_start",nullptr,ChoiceValues::Index},
        // System Settings: System Language is the page's one choice row this build lists
        {"system","console_lang","page_console_system_lang",nullptr,ChoiceValues::Index},
        // Theme Settings: all four pages are empty in the RCO (9.3f); Theme's list is the captured one
        {"theme","theme","page_theme_config_theme",
            "the Page has no children at all; the list is built by the plugin at run time (captured labels)",ChoiceValues::Index},
        {"theme","color","page_theme_config_color",
            "the Page has no LowerMenu items; the plugin builds the colour chips at run time (12 captured, labels not transcribed)",ChoiceValues::Index},
        {"theme","background","page_theme_config_background",
            "the Page has no LowerMenu items; the plugin builds the list at run time (contents not transcribed)",ChoiceValues::Index},
        {"theme","font","page_theme_config_font",
            "the Page has no LowerMenu items; the plugin builds the list at run time (contents not transcribed)",ChoiceValues::Index},
        // Date and Time (9.2: winter/summer row is Daylight Saving in the US build)
        {"date","autodate","page_date_time_config_automatic_setting",nullptr,ChoiceValues::Bool},
        {"date","date_fmt","page_date_time_config_date_format",nullptr,ChoiceValues::DateFormat},
        {"date","time_fmt","page_date_time_config_time_format",nullptr,ChoiceValues::HourFormat},
        {"date","timezone","page_date_time_config_timezone",nullptr,ChoiceValues::Zone},
        {"date","summertime","page_date_time_config_summer_time",nullptr,ChoiceValues::Index},
        // Power Save (System Auto-Off opens a page of two timers; no option list is shipped or captured)
        {"power","idle","page_console_auto_power_off_option",
            "the Page has a Plane but no LowerMenu items; the timer list is built at run time and was not captured",ChoiceValues::Index},
        {"power","controller",nullptr,"no page object for Controller Auto-Off in sysconf_plugin.rco",ChoiceValues::Index},
        // Accessory Settings (seg_device)
        {"device","pd_keyboard_type","page_device_config_keyboard_type",nullptr,ChoiceValues::Index},
        {"device","pd_keyboard_input",nullptr,"no page object for Keyboard Entry Method in sysconf_plugin.rco",ChoiceValues::Index},
        {"device","pd_keyrepeat_start","page_device_config_key_repeat_start_time",nullptr,ChoiceValues::Index},
        {"device","pd_keyrepeat_speed","page_device_config_key_repeat_speed",nullptr,ChoiceValues::Index},
        {"device","pd_mouse_type","page_device_config_mouse_type",nullptr,ChoiceValues::Index},
        {"device","pd_pointer_speed","page_device_config_pointer_speed",nullptr,ChoiceValues::Index},
        {"device","pd_pad_vibration",nullptr,"no page object for Controller Vibration Function in sysconf_plugin.rco",ChoiceValues::Index},
        // Display Settings (9.2 values: After 20 Minutes, Off, Limited, Off, Automatic)
        {"display","screensaver","page_display_config_screensaverstartup",nullptr,ChoiceValues::ScreenMinutes},
        {"display","cross_color_filter","page_display_config_cross_color_filter",nullptr,ChoiceValues::Bool},
        {"display","rgb_full_range","page_display_config_rgb_full_range",nullptr,ChoiceValues::Index},
        {"display","super_white","page_display_config_super_white",nullptr,ChoiceValues::Bool},
        {"display","deep_color","page_display_config_deep_color_output",nullptr,ChoiceValues::Index},
        // Sound Settings (9.2: Audio Multi-Output Off, Key Tone On)
        {"sound","audio_multi_output","page_sound_config_multi_output",nullptr,ChoiceValues::Bool},
        {"sound","keytone","page_sound_config_operation_sound",nullptr,ChoiceValues::Bool},
        // Security Settings (9.2: BD Off, region United States, DVD Off, Parental Level 9, Browser Off)
        {"security","bd_pc","page_security_config_bd_parental_control",nullptr,ChoiceValues::Bool},
        {"security","bd_dvd_pcregion","page_security_config_dvd_region_code_control",nullptr,ChoiceValues::Index},
        {"security","dvd_pc","page_security_config_dvd_parental_control",nullptr,ChoiceValues::Index},
        {"security","gameetc_pc","page_security_config_parental_lock_level",nullptr,ChoiceValues::Index},
        {"security","pc_browser","page_security_config_browser",nullptr,ChoiceValues::Bool},
        // Remote Play (9.2: Remote Start Off, Audio Output Device = Remote Play Device)
        {"remote","remote_start","page_remote_config_remote_boot",nullptr,ChoiceValues::Bool},
        {"remote","audio_output_device","page_remote_config_audio_config",nullptr,ChoiceValues::Index},
        // Network Settings (the status list is an information row; the setup/test rows are flows)
        {"network","net_internet","page_network_config_internet_connect",nullptr,ChoiceValues::Bool},
        {"network","net_media_server","page_network_config_mediaserver_connection_config",nullptr,ChoiceValues::Bool}};
    return table;
}
const ChoiceBinding* choiceBinding(const QString& pageId,const QString& rowId) {
    for(const auto& binding:choiceBindings())
        if(pageId==QLatin1String(binding.page) && rowId==QLatin1String(binding.row))return &binding;
    return nullptr;
}
QVariant firmwareChoiceValue(ChoiceValues kind,int index,const QString& label,const QString& item) {
    switch(kind) {
    case ChoiceValues::Bool:
        if(label==QLatin1String("Off"))return false;
        if(label==QLatin1String("On"))return true;
        return index;
    case ChoiceValues::DateFormat: {
        static const int values[]{2,0,1};  // authored order: YYYY/MM/DD, DD/MM/YYYY, MM/DD/YYYY
        return index<int(std::size(values))?values[index]:index;
    }
    case ChoiceValues::HourFormat: {
        static const int values[]{1,0};    // authored order: 12-Hour Clock, 24-Hour Clock
        return index<int(std::size(values))?values[index]:index;
    }
    case ChoiceValues::ScreenMinutes: {
        static const int values[]{5,10,20,30,0};
        return index<int(std::size(values))?values[index]:index;
    }
    case ChoiceValues::Zone: {
        // The zone's own item name without its "item_" namespace: a stable, localization-free token.
        // clockDateTime resolves it through the item's msg_timezone_* text (the GMT offset in the label),
        // because the firmware's 110 zones are offset zones, not IANA identities.
        return item.startsWith(QLatin1String("item_"))?item.mid(5):item;
    }
    case ChoiceValues::Index: break;
    }
    return index;
}
// The firmware's time zones are offset zones: every label states "GMT+HH:MM" (SETTINGS 5.7), and
// clock/timeZone stores the zone's item token rather than an IANA id (11.3). This resolves a token back to
// the RCO's own label and the offset that label states, through the same English table the band's items
// are drawn from. `known` is false for anything that is not a token this firmware ships - a stale token, or
// a well-formed name in the namespace with no msg_timezone_* entry - so callers say so instead of
// inventing a zone (SETTINGS 14).
struct FirmwareZone {
    QString label;          // the item's own text, e.g. "GMT-08:00 Pacific Time (US, Canada)"
    int offsetSeconds=0;
    bool known=false;       // the token resolved to an RCO label that states a GMT offset
};
FirmwareZone firmwareZoneFor(Assets& assets,const QString& token) {
    static QHash<QString,FirmwareZone> cache;
    if(const auto found=cache.constFind(token);found!=cache.cend())return *found;
    FirmwareZone zone;
    if(Preferences::isFirmwareZoneToken(token)) {
        zone.label=assets.text(QStringLiteral("sysconf_plugin"),
            QStringLiteral("msg_timezone_")+token.mid(Preferences::firmwareZoneTokenPrefix().size()));
        static const QRegularExpression pattern(QStringLiteral("GMT([+-])(\\d{1,2}):(\\d{2})"));
        const auto match=pattern.match(zone.label);
        if(match.hasMatch()) {
            const int sign=match.captured(1)==QLatin1String("-")?-1:1;
            zone.offsetSeconds=sign*(match.captured(2).toInt()*3600+match.captured(3).toInt()*60);
            zone.known=true;
        }
    }
    cache.insert(token,zone);
    return zone;
}
// The MItem's own label: its `text` reference resolved through the container's English table, then
// `altText` as the second choice. Empty when the entry has neither (never happens in sysconf_plugin).
QString choiceItemLabel(const RcoObjects::Container& container,const RcoObjects::Object& item) {
    for(const auto& property:container.properties(item)) {
        if(property.name!=QLatin1String("text") && property.name!=QLatin1String("altText"))continue;
        if(property.value.refKind()!=RcoObjects::RefKind::Text)continue;
        const auto text=container.text(property.value.target);
        if(!text.isEmpty())return text;
    }
    return {};
}
// A page's authored items, plus the page's own Scroll object when the RCO declares one. The RCO carries
// a Scroll child on only a few pages (first-config language/timezone, characterset, the utility lists);
// none of the settings choice pages has one, so scrollability also comes from the measured band: 40 px
// pitch over 1080 px holds 27 items (SETTINGS 9.1/U2), and a longer list must scroll.
struct RcoChoicePage {
    const RcoObjects::Container* container=nullptr;
    const RcoObjects::Object* page=nullptr;
    QVector<const RcoObjects::Object*> items;
    QString scrollObject;
    QString error;
};
RcoChoicePage readChoicePage(Assets& assets,const QString& containerName,const QString& pageName) {
    RcoChoicePage result;
    const auto& container=choiceObjects(assets).container(containerName);
    result.container=&container;
    if(!container.ok) {result.error=container.error.isEmpty()?QStringLiteral("container unreadable"):container.error;return result;}
    const auto* page=container.object(pageName);
    if(!page) {result.error=QStringLiteral("no object named ")+pageName;return result;}
    result.page=page;
    const RcoObjects::Object* plane=nullptr;
    for(const auto* child:container.children(*page)) {
        if(child->type==QLatin1String("Plane")){plane=child;break;}
    }
    if(!plane)return result;  // an empty Page: the plugin builds the list at run time
    for(const auto* child:container.children(*plane)) {
        if(child->type==QLatin1String("Scroll")&&result.scrollObject.isEmpty())result.scrollObject=child->name;
    }
    const RcoObjects::Object* menu=nullptr;
    for(const auto* child:container.children(*plane)) {
        if(child->type==QLatin1String("MList")){menu=child;break;}
    }
    if(!menu)return result;
    for(const auto* child:container.children(*menu)) {
        if(child->type==QLatin1String("MItem"))result.items.push_back(child);
    }
    return result;
}
// The captured Theme list: page_theme_config_theme is an empty Page in the RCO (9.3f), so the labels are
// the 2026-10-28 capture's (settings/theme-choice.png). Only Original is installable in this build: the
// other three are firmware themes this build has no asset for, and Install is the native file picker.
ChoiceList themeCaptureChoices(const QVariant& current) {
    ChoiceList list;
    list.pageId=QStringLiteral("theme");list.rowId=QStringLiteral("theme");list.rowTitle=QStringLiteral("Theme");
    list.setting=QStringLiteral("appearance/theme");list.rcoPage=QStringLiteral("page_theme_config_theme");
    list.source=QStringLiteral("capture");
    list.note=QStringLiteral("page_theme_config_theme is an empty Page in sysconf_plugin.rco (0 children), so the "
        "plugin builds this list at run time; these four labels are the capture batch's "
        "(out/rpcs3-ref-2026-10-28/settings/theme-choice.png). Original is the only item this build can install; "
        "Install is the native .p3t picker and Classic/Air Paint ship no asset here.");
    struct Spec {const char* label;bool writes;};
    static const Spec specs[]{{"Install",false},{"Original",true},{"Classic",false},{"Air Paint",false}};
    int index=0;
    for(const auto& spec:specs) {
        ChoiceItem item;
        item.label=QString::fromLatin1(spec.label);
        item.source=QStringLiteral("capture");
        item.rcoIndex=index++;
        item.writes=spec.writes;
        if(spec.writes) {
            item.value=QString{};
            item.isCurrent=choiceValueMatches(current,item.value);
        }
        list.items.push_back(item);
    }
    return list;
}
// A list this build's own choices() produces, because the firmware has no shipped list: an empty RCO
// page, a native-only row, or a row whose page object is absent. The labels are this build's, marked as
// such - never presented as the firmware's.
ChoiceList nativeChoiceList(const QString& pageId,const Entry& row,Assets& assets,
                            const QVariant& current,const QString& source,const QString& note) {
    ChoiceList list;
    list.pageId=pageId;list.rowId=row.id;list.rowTitle=row.title;list.setting=row.setting;
    list.source=source;list.note=note;
    if(row.setting.isEmpty())return list;
    int index=0;
    for(const auto& choice:choices(row.setting,assets)) {
        ChoiceItem item;
        item.label=choice.title;item.value=choice.value;item.source=source;item.rcoIndex=index++;
        item.writes=true;
        item.isCurrent=choiceValueMatches(current,choice.value);
        list.items.push_back(item);
    }
    return list;
}
// The list's current row, and the one log line that says which case a time zone is in. `currentRow` is the
// item the persisted value selects - the row the band focuses - or -1 when nothing in the list is current.
// The time zone is the case this exists for (SETTINGS 13.6a): a stored firmware token is one of the 110
// items, an IANA id is none of them, and a value that matches no item must not be guessed into a row, so
// the model reports -1 and says so. `current` is invalid on the draw path that builds the catalogue
// without a store value (View::choiceBandFor), which is not a "no current row" verdict and is not logged.
void markCurrentRow(ChoiceList& list,const QVariant& current) {
    list.currentRow=-1;
    for(const auto& item:list.items)if(item.writes && item.isCurrent){list.currentRow=item.rcoIndex;break;}
    if(list.setting!=QLatin1String("clock/timeZone") || list.items.isEmpty() || !current.isValid())return;
    static QSet<QString> logged;
    const auto text=current.toString();
    if(logged.contains(text))return;
    logged.insert(text);
    if(list.currentRow>=0) {
        QString label;
        for(const auto& item:list.items)if(item.rcoIndex==list.currentRow)label=item.label;
        qInfo().noquote()<<QStringLiteral("settings: clock/timeZone %1 is firmware zone item %2 of %3 (%4)")
            .arg(text).arg(list.currentRow).arg(list.items.size()).arg(label);
        return;
    }
    if(Preferences::isFirmwareZoneToken(text))
        qWarning().noquote()<<QStringLiteral("settings: clock/timeZone %1 is a firmware zone token this "
            "firmware does not ship; no current row (the clock follows the host and the band marks none)").arg(text);
    else
        qInfo().noquote()<<QStringLiteral("settings: clock/timeZone %1 is not one of the firmware's %2 zone "
            "items; no current row (the band marks no item as current)").arg(text).arg(list.items.size());
}
// One row's list. `entry` is the page's own row (id, title, setting, action), so a row's write path and
// its list cannot disagree.
ChoiceList buildChoiceList(const QString& pageId,const Entry& entry,Assets& assets,const QVariant& current) {
    ChoiceList list;
    list.pageId=pageId;list.rowId=entry.id;list.rowTitle=entry.title;list.setting=entry.setting;
    const auto* binding=choiceBinding(pageId,entry.id);
    if(!binding) {
        // A row with no firmware counterpart that this build still makes a choice: the three media
        // settings and Photo Settings. Its list is choices()'s, marked native.
        if(entry.action!=Action::SettingChoices || entry.setting.isEmpty())return list;
        auto native=nativeChoiceList(pageId,entry,assets,current,QStringLiteral("native"),
            QStringLiteral("this build's own setting: the sysconf inventory has no page for it"));
        markCurrentRow(native,current);
        return native;
    }
    list.rcoPage=QString::fromLatin1(binding->rcoPage?binding->rcoPage:"");
    if(binding->rcoPage)list.container=QStringLiteral("sysconf_plugin");
    const bool verbose=qEnvironmentVariableIsSet("XMB_XMBML_LOG") && qEnvironmentVariable("XMB_XMBML_LOG")!=QStringLiteral("0");
    if(!binding->rcoPage) {
        list.source=QStringLiteral("none");
        list.note=QString::fromLatin1(binding->note);
        if(verbose)qInfo().noquote()<<"settings: no choice page for"<<pageId+"/"+entry.id<<"("<<binding->note<<")";
        return list;
    }
    const auto page=readChoicePage(assets,QStringLiteral("sysconf_plugin"),list.rcoPage);
    if(!page.error.isEmpty()) {
        list.source=QStringLiteral("none");
        list.note=QStringLiteral("sysconf_plugin.rco: ")+page.error;
        qWarning().noquote()<<"settings: choice page"<<list.rcoPage<<"unreadable:"<<page.error;
        return list;
    }
    if(pageId==QLatin1String("theme") && entry.id==QLatin1String("theme")) {
        auto captured=themeCaptureChoices(current);
        captured.rcoPageOffset=page.page?page.page->offset:0;
        markCurrentRow(captured,current);
        return captured;
    }
    list.rcoPageOffset=page.page?page.page->offset:0;
    list.scrollObject=page.scrollObject;
    if(page.items.isEmpty()) {
        // The page exists but ships no items: the run-time-built case (SETTINGS 3.3/9.3f). A row this
        // build can write falls back to its own choices(); the note says exactly which fallback this is.
        const auto why=binding->note?QString::fromLatin1(binding->note)
            :QStringLiteral("the page object has no LowerMenu items");
        if(entry.setting.isEmpty()||entry.action!=Action::SettingChoices) {
            list.source=QStringLiteral("rco-page-empty");list.note=why;
            return list;
        }
        auto fallback=nativeChoiceList(pageId,entry,assets,current,QStringLiteral("native"),
            why+QStringLiteral("; the list below is this build's, not the firmware's"));
        // Keep the page object's identity even though its items are this build's: the provenance of a
        // fallback is both facts - which page was empty, and where the fallback came from.
        fallback.rcoPage=list.rcoPage;fallback.rcoPageOffset=list.rcoPageOffset;
        fallback.container=QStringLiteral("sysconf_plugin");fallback.scrollObject=list.scrollObject;
        markCurrentRow(fallback,current);
        return fallback;
    }
    list.source=QStringLiteral("rco");
    list.note=binding->note?QString::fromLatin1(binding->note):QString{};
    const auto& container=*page.container;
    int index=0;
    for(const auto* object:page.items) {
        ChoiceItem item;
        item.label=choiceItemLabel(container,*object);
        item.source=QStringLiteral("rco");
        item.item=object->name;
        item.rcoIndex=index++;
        item.rcoOffset=object->offset;
        item.value=firmwareChoiceValue(binding->values,item.rcoIndex,item.label,item.item);
        item.writes=!entry.setting.isEmpty() && item.value.isValid();
        item.isCurrent=item.writes && choiceValueMatches(current,item.value);
        list.items.push_back(item);
    }
    list.scrollable=!list.scrollObject.isEmpty() || list.items.size()>kBandRows;
    for(auto& item:list.items)item.scrollable=list.scrollable;
    markCurrentRow(list,current);
    if(verbose)qInfo().noquote()<<"settings: choice list"<<pageId+"/"+entry.id<<"rco="+list.rcoPage
        <<"items="<<list.items.size()<<"source="<<list.source;
    return list;
}
QVector<ChoiceList> choiceListsForRows(const QString& pageId,const QVector<Entry>& rows,Assets& assets,
                                       const std::function<QVariant(const QString&)>& currentFor) {
    QVector<ChoiceList> lists;
    for(const auto& entry:rows) {
        const auto current=entry.setting.isEmpty()||!currentFor?QVariant{}:currentFor(entry.setting);
        auto list=buildChoiceList(pageId,entry,assets,current);
        // A row is a choice row when it has a page, a note about a missing page, or a native list.
        if(list.items.isEmpty() && list.source.isEmpty())continue;
        lists.push_back(std::move(list));
    }
    return lists;
}
// Photo Settings is this build's own page (the sysconf inventory has no photo page): its three choice
// rows are listed by the catalogue so "no firmware page" is on the record rather than an omission.
QVector<Entry> photoChoiceRows() {
    QVector<Entry> rows;
    rows.push_back(option("interval","Slideshow Interval","","photo/interval"));
    rows.push_back(option("fit","Display Mode","","photo/fit"));
    rows.push_back(option("repeat","Repeat","","media/repeat"));
    return rows;
}
QJsonObject choiceItemJson(const ChoiceItem& item) {
    QJsonValue value=QJsonValue::Null;
    switch(item.value.metaType().id()) {
    case QMetaType::Bool: value=item.value.toBool();break;
    case QMetaType::Int: value=item.value.toInt();break;
    case QMetaType::QString: value=item.value.toString();break;
    default: break;
    }
    return QJsonObject{{"label",item.label},{"value",value},{"isCurrent",item.isCurrent},
        {"source",item.source},{"scrollable",item.scrollable},{"item",item.item},
        {"rcoIndex",item.rcoIndex},{"rcoOffset",qint64(item.rcoOffset)},{"writes",item.writes}};
}
QJsonObject choiceListJson(const ChoiceList& list) {
    QJsonArray items;
    for(const auto& item:list.items)items.push_back(choiceItemJson(item));
    return QJsonObject{{"row",list.rowId},{"rowTitle",list.rowTitle},{"setting",list.setting},
        {"rcoPage",list.rcoPage},{"rcoPageOffset",qint64(list.rcoPageOffset)},
        {"container",list.container},{"source",list.source},{"note",list.note},
        {"scrollable",list.scrollable},{"scrollObject",list.scrollObject},
        {"currentRow",list.currentRow},{"count",list.count()},{"items",items}};
}
// The Settings-column page order (SETTINGS 1.1/10.2), for the aggregate catalogue.
const QStringList& choicePageOrder() {
    static const QStringList pages{"game","video","music","chat","system","theme","date","power","device",
        "print","display","sound","security","remote","network","photo"};
    return pages;
}
// The aggregate catalogue: every page's choice rows in one file, so a run that opens any settings page
// records the whole choice model with its provenance. Written once per process.
void dumpChoiceCatalogue(Assets& assets,const std::function<QVariant(const QString&)>& currentFor) {
    if(!qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES"))return;
    static bool written=false;
    if(written)return;
    written=true;
    QJsonArray pages;
    QString containerName,containerPath,containerVersion;
    qint64 containerBytes=0;
    for(const auto& pageId:choicePageOrder()) {
        QVector<Entry> rows;
        QString title;
        if(pageId==QLatin1String("photo")) {rows=photoChoiceRows();title=QStringLiteral("Photo Settings");}
        else {
            auto data=pageRows(pageId,assets,QString());
            rows=data.entries;title=data.title;
        }
        const auto lists=choiceListsForRows(pageId,rows,assets,currentFor);
        QJsonArray choices;
        for(const auto& list:lists) {
            choices.push_back(choiceListJson(list));
            if(containerName.isEmpty() && list.source==QLatin1String("rco"))containerName=list.container;
        }
        pages.push_back(QJsonObject{{"page",pageId},{"title",title},
            {"rows",int(rows.size())},{"choices",choices},{"choiceRows",int(lists.size())}});
    }
    // The container the lists were read from, with its own identity (name, path, version, byte size), so
    // every rcoPageOffset above can be cited against a known file.
    if(!containerName.isEmpty()) {
        const auto& container=choiceObjects(assets).container(containerName);
        containerPath=container.path;containerBytes=container.fileBytes;
        containerVersion=QString::number(container.version);
    }
    QJsonObject object{{"catalogue",true},{"bandRows",kBandRows},{"bandPitch",40},
        {"container",containerName},{"containerPath",containerPath},{"containerVersion",containerVersion},
        {"containerBytes",containerBytes},{"pages",pages}};
    QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".settings-choices.json");
    if(file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(object).toJson());
        qInfo().noquote()<<"settings: wrote the choice catalogue"<<file.fileName()<<"("<<pages.size()<<" pages)";
    } else qWarning().noquote()<<"settings: cannot write the choice catalogue";
    // Exercise the single-list API a view pass will call, by row id and by preference key, so a dump run
    // proves the public entry points too and not only the catalogue's own path.
    const auto byRow=settingsChoices(QStringLiteral("display"),QStringLiteral("screensaver"),assets,
        currentFor?currentFor(QStringLiteral("display/screensaverMinutes")):QVariant{});
    const auto byKey=settingsChoices(QStringLiteral("date"),QStringLiteral("clock/dateFormat"),assets,
        currentFor?currentFor(QStringLiteral("clock/dateFormat")):QVariant{});
    qInfo().noquote()<<QStringLiteral("settings: settingsChoices API -> display/screensaver %1 items, "
        "date/clock-dateFormat %2 items").arg(byRow.size()).arg(byKey.size());
}
// Verification hook (the same XMB_DUMP_ENTRIES the shell's catalogue/folder/store dumps use): every
// settings page opened in a scripted run writes its row list, so a capture can be checked without pixels.
// Phase 11.3 adds each row's choice list with its provenance (RCO page, item names and offsets).
void dumpSettingsPage(const PageRows& data,Assets& assets,
                      const std::function<QVariant(const QString&)>& currentFor) {
    if(!qEnvironmentVariableIsSet("XMB_DUMP_ENTRIES"))return;
    QJsonArray rows;
    for(const auto& entry:data.entries) {
        rows.push_back(QJsonObject{{"id",entry.id},{"title",entry.title},{"detail",entry.detail},
            {"action",int(entry.action)},{"setting",entry.setting},{"value",entry.value.left(80)},
            {"texture",entry.texture},{"source",entry.source},
            {"nativeOnly",data.nativeOnly.contains(entry.id)}});
    }
    QJsonArray choices;
    for(const auto& list:choiceListsForRows(data.id,data.entries,assets,currentFor))choices.push_back(choiceListJson(list));
    QJsonObject object{{"page",data.id},{"title",data.title},{"count",int(data.entries.size())},
        {"rows",rows},{"firmware",data.firmware},{"choices",choices},
        {"skipped",QJsonArray::fromStringList(data.notes)}};
    QFile file(qEnvironmentVariable("XMB_DUMP_ENTRIES")+".settings-"+data.id+".json");
    if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(object).toJson());
    else qWarning().noquote()<<"settings: cannot write the"<<data.id<<"page dump";
}
// --- The time zone diagnostic (SETTINGS-FIRMWARE.md 14) -------------------------------------------
// The bug this proves fixed: a stored firmware zone token used to be rejected by Preferences::validBare,
// so it could never survive a save/load and the 110-item band could never mark its current row (13.6a).
// Run with XMB_TZ_SELFCHECK=<directory>; any settings page open triggers it once. It is deliberately not
// a second implementation of the rules: it stores through Preferences, reloads through a second
// Preferences on the same file, resolves the band's list through settingsChoiceLists(), reads the row's
// text through Shell::valueText() (passed in) and applies the band's own focus rule (the item whose label
// the row's displayed value names, SETTINGS 13.4). What it reports is what the app does, not what it
// should do. The shell's own store is put back the way it was before the function returns.
QJsonObject runTimeZoneSelfCheck(Assets& assets,Preferences& store,
    const std::function<QString(const Entry&)>& valueText,const std::function<QString()>& clockText,
    const QString& root) {
    const QString token=QStringLiteral("date_time_config_timezone_pacific_us_canada");
    const QString iana=QStringLiteral("America/Los_Angeles");
    QJsonObject report{{"selfCheck",QStringLiteral("clock/timeZone firmware zone token")},
        {"token",token},{"iana",iana}};
    QDir().mkpath(root);
    const auto writeJson=[&](const QString& path,const QJsonObject& object) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson())>0;
    };
    const auto readJson=[&](const QString& path) {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly))return QJsonObject{};
        return QJsonDocument::fromJson(file.readAll()).object();
    };
    // (a) store -> new process-equivalent load. A second Preferences on the same directory is the same
    // read path a restart takes: the file is parsed and every value is validated before it is kept.
    const auto loadDir=root+QStringLiteral("/load");
    QDir().mkpath(loadDir);
    QFile::remove(loadDir+QStringLiteral("/preferences.json"));
    bool stored=false;
    {Preferences writer(loadDir);stored=writer.setValue("clock/timeZone",token);}
    QString reloaded;QJsonObject loadedFile;
    {Preferences reader(loadDir);
     reloaded=reader.value("clock/timeZone").toString();loadedFile=readJson(reader.path());}
    const auto loadedValues=loadedFile.value("values").toObject();
    report["load"]=QJsonObject{{"accepted",stored},{"reloaded",reloaded},{"kept",reloaded==token},
        {"fileVersion",loadedFile.value("version").toInt(-1)},
        {"storedUnderSharedKey",loadedValues.contains("clock/timeZone")},
        {"value",loadedValues.value("clock/timeZone").toString()}};
    // (a) the version-1 file a pre-profile install wrote: the shared time zone stays bare and the per-user
    // key moves under the reserved profile, both still validated by the same rule that now accepts a token.
    const auto migrateDir=root+QStringLiteral("/migrate");
    QDir().mkpath(migrateDir);
    QFile::remove(migrateDir+QStringLiteral("/preferences.json"));
    const QJsonObject legacy{{"version",1},{"values",QJsonObject{{"clock/timeZone",token},
        {"audio/volume",30},{"system/name",QStringLiteral("TEST")}}}};
    writeJson(migrateDir+QStringLiteral("/preferences.json"),legacy);
    QString migratedZone;QJsonObject migratedFile;int migratedVolume=-1;bool migratedProfileKey=false;bool legacyKeyGone=false;
    {Preferences migrated(migrateDir);
     migratedZone=migrated.value("clock/timeZone").toString();
     migratedVolume=migrated.value("audio/volume").toInt();
     migratedFile=readJson(migrated.path());}
    {const auto values=migratedFile.value("values").toObject();
     migratedProfileKey=values.contains("profile/local/audio/volume");
     legacyKeyGone=!values.contains("audio/volume");}
    report["migration"]=QJsonObject{{"from","version 1"},{"reloaded",migratedZone},
        {"kept",migratedZone==token},{"fileVersion",migratedFile.value("version").toInt(-1)},
        {"volume",migratedVolume},{"volumeUnderProfile",migratedProfileKey},
        {"bareVolumeGone",legacyKeyGone},
        {"sharedKeyStayedBare",migratedFile.value("values").toObject().contains("clock/timeZone")}};
    // (1) bounded: values that are not one of the three accepted forms are refused, and a refusal leaves
    // the stored value alone (the same contract setValues() has).
    const QStringList refused{
        QStringLiteral("date_time_config_timezone_"),                 // empty zone name
        QStringLiteral("date_time_config_timezone_9lives"),           // name does not start with a letter
        QStringLiteral("date_time_config_timezone_pacific_us_canada_"),// trailing underscore
        QStringLiteral("date_time_config_timezone_Pacific_US_Canada"),// upper case
        QStringLiteral("date_time_config_timezone_pacific us canada"),// a space
        QStringLiteral("Pacific Time (US, Canada)"),                  // the label, not the token
        QStringLiteral("Europe/Nowhere"),                             // not an IANA id
        QStringLiteral("system"),                                     // not "System"
        token+QStringLiteral("x").repeated(48)};                      // longer than the bound
    QJsonArray refusedReport;
    int refusedCount=0;
    {Preferences bounded(loadDir);
     for(const auto& value:refused) {
         const bool accepted=bounded.setValue("clock/timeZone",value);
         if(!accepted)++refusedCount;
         refusedReport.push_back(QJsonObject{{"value",value},{"accepted",accepted}});
     }}
    report["refused"]=QJsonObject{{"count",refusedCount},{"of",int(refused.size())},{"values",refusedReport}};
    // (b)/(c): the band's current row and the row's own text, for a token and for an IANA id, through the
    // shell's store and the shell's valueText.
    const Entry row=option(QStringLiteral("timezone"),assets.text(QStringLiteral("sysconf_plugin"),
        QStringLiteral("msg_timezone"),QStringLiteral("Time Zone")),{},QStringLiteral("clock/timeZone"));
    const auto listFor=[&](const QVariant& current) {
        const auto lists=settingsChoiceLists(QStringLiteral("date"),assets,
            [&current](const QString& key){return key==QLatin1String("clock/timeZone")?current:QVariant{};});
        for(const auto& list:lists)if(list.setting==QLatin1String("clock/timeZone"))return list;
        return ChoiceList{};
    };
    // The band's own focus rule (SETTINGS 13.4, rule 2): the drawn row whose label the row's displayed value
    // names. -1 means the displayed value names no firmware item, so no row is current.
    const auto bandFocus=[&](const ChoiceList& list,const QString& text) {
        if(text.isEmpty())return -1;
        for(const auto& item:list.items)if(item.label==text)return item.rcoIndex;
        return -1;
    };
    const QVariant original=store.value("clock/timeZone");
    QJsonObject tokenCase,ianaCase;
    const auto caseFor=[&](const QString& value,const char* name) {
        const bool accepted=store.setValue("clock/timeZone",value);
        const auto stored=store.value("clock/timeZone").toString();
        const auto list=listFor(stored);
        const auto text=valueText(row);
        const int focus=bandFocus(list,text);
        QJsonObject result{{"stored",stored},{"accepted",accepted},{"currentRow",list.currentRow},
            {"items",list.count()},{"valueText",text},{"bandFocus",focus},
            {"focusNamesTheCurrentRow",focus>=0 && focus==list.currentRow},
            {"clock",clockText?clockText():QString{}}};
        if(focus>=0)for(const auto& item:list.items)if(item.rcoIndex==focus)result["bandFocusLabel"]=item.label;
        // Every token the firmware's list produces must pass the store's rule, or a band item could not be
        // written back after a Cross: count them from the same list the band draws.
        if(name==QLatin1String("token")) {
            int acceptedItems=0;
            for(const auto& item:list.items)if(Preferences::isFirmwareZoneToken(item.value.toString()))++acceptedItems;
            result["tokenItemsAcceptedByStore"]=acceptedItems;
        }
        return result;
    };
    tokenCase=caseFor(token,"token");
    ianaCase=caseFor(iana,"iana");
    store.setValue("clock/timeZone",original);
    report["tokenCase"]=tokenCase;
    report["ianaCase"]=ianaCase;
    report["storeRestored"]=store.value("clock/timeZone")==original;
    const auto path=QDir(root).filePath(QStringLiteral("tz-selfcheck.json"));
    report["path"]=path;
    report["written"]=writeJson(path,report);
    // The log is the same eight facts in one place, so a run can be read without opening the JSON.
    qInfo().noquote()<<QStringLiteral("clock selfcheck: token %1 stores=%2 reloads=%3 migrates=%4 "
        "(v1->v2, %5) -> band current row %6 of %7, valueText \"%8\", clock \"%9\"")
        .arg(token).arg(stored?1:0).arg(reloaded==token?1:0).arg(migratedZone==token?1:0)
        .arg(migratedFile.value("version").toInt(-1))
        .arg(tokenCase.value("currentRow").toInt(-1)).arg(tokenCase.value("items").toInt())
        .arg(tokenCase.value("valueText").toString(),tokenCase.value("clock").toString());
    qInfo().noquote()<<QStringLiteral("clock selfcheck: iana %1 stores=1, no firmware zone item -> current "
        "row %2 (band focus %3), valueText \"%4\", clock \"%5\"")
        .arg(iana).arg(ianaCase.value("currentRow").toInt(-1)).arg(ianaCase.value("bandFocus").toInt(-1))
        .arg(ianaCase.value("valueText").toString(),ianaCase.value("clock").toString());
    qInfo().noquote()<<QStringLiteral("clock selfcheck: refused %1 of %2 non-zone values (bounded form); "
        "wrote %3").arg(refusedCount).arg(int(refused.size())).arg(path);
    return report;
}
} // namespace

// The firmware's own choice list for one settings row (Phase 11.3). `rowId` names the row on `pageId`
// or its Preferences key; the list is the page's LowerMenu in document order, exactly as the RCO
// authors it. An empty result means the row is not a choice row (or its page could not be read, which
// is logged); a non-empty result with `source` "none" is a row whose firmware choice page is missing.
QVector<ChoiceItem> settingsChoices(const QString& pageId,const QString& rowId,Assets& assets,
                                    const QVariant& current) {
    QVector<Entry> rows;
    if(pageId==QLatin1String("photo"))rows=photoChoiceRows();
    else {auto data=pageRows(pageId,assets,QString());rows=data.entries;}
    if(rows.isEmpty())return {};
    // The row may be named by its id (the view's normal case) or by the Preferences key it writes (the
    // shorthand `settingsChoices("date","clock/dateFormat")`), so a caller with either can ask.
    const Entry* found=nullptr;
    for(const auto& row:rows)
        if(row.id==rowId || (!row.setting.isEmpty() && row.setting==rowId)){found=&row;break;}
    if(!found)return {};
    const auto list=buildChoiceList(pageId,*found,assets,current);
    return list.items;
}
QVector<ChoiceList> settingsChoiceLists(const QString& pageId,Assets& assets,
    const std::function<QVariant(const QString&)>& currentFor) {
    QVector<Entry> rows;
    if(pageId==QLatin1String("photo"))rows=photoChoiceRows();
    else {auto data=pageRows(pageId,assets,QString());rows=data.entries;}
    if(rows.isEmpty())return {};
    return choiceListsForRows(pageId,rows,assets,currentFor);
}
QVector<ChoiceList> settingsChoiceCatalogue(Assets& assets,
    const std::function<QVariant(const QString&)>& currentFor) {
    QVector<ChoiceList> all;
    for(const auto& pageId:choicePageOrder()) {
        QVector<Entry> rows;
        if(pageId==QLatin1String("photo"))rows=photoChoiceRows();
        else {auto data=pageRows(pageId,assets,QString());rows=data.entries;}
        all+=choiceListsForRows(pageId,rows,assets,currentFor);
    }
    return all;
}
bool choiceValueMatches(const QVariant& current,const QVariant& value) {
    if(!current.isValid() || !value.isValid())return false;
    if(current==value)return true;
    // A preference may hold an int where the choice is a bool or a string ("5" vs 5): compare the numeric
    // and text forms before saying no, so the current-value mark cannot disappear on a QVariant type.
    bool currentNumeric=false,valueNumeric=false;
    const qlonglong a=current.toLongLong(&currentNumeric),b=value.toLongLong(&valueNumeric);
    if(currentNumeric && valueNumeric)return a==b;
    return current.toString()==value.toString();
}

// The Settings column, resolved from the firmware's own definition. See settings_menu.h for the contract;
// audit/SETTINGS-FIRMWARE.md 1.1-1.2 and 9 list what each row is and what was omitted.
QVector<Entry> settingsColumnEntries(const QVector<Entry>& builtin,Assets& assets) {
    QHash<QString,Entry> byId;
    for(const auto& entry:builtin)byId.insert(entry.id,entry);
    const bool verbose=qEnvironmentVariableIsSet("XMB_XMBML_LOG") && qEnvironmentVariable("XMB_XMBML_LOG")!=QStringLiteral("0");
    XmbmlResolveOptions options;
    options.flashRoot=assets.root();
    // The ids this build already uses for these pages (and for their theme icons), so every hunk that
    // addresses a settings row by id keeps working (MENUS-XMBML 4.2).
    options.idAliases={{"console","system"},{"bd_dvd","video"},{"date_time","date"}};
    QElapsedTimer timer;timer.start();
    const auto column=resolveCategoryColumn(QStringLiteral("category_sysconf"),
        [&assets](const QString& container,const QString& key){
            const auto value=assets.text(container,key);
            if(!value.isEmpty())return value;
            return assets.text("sysconf_plugin",key);
        },options);
    if(!column.ok()) {
        qWarning().noquote()<<"xmbml: settings column keeps its built-in rows:"<<column.error<<column.path;
        return {};
    }
    // The two rows a retail console does not show (SETTINGS 1.2, MENUS-XMBML 3.1): the console gates them
    // in the sysconf plugin - the Japan-only Edy Viewer and the QA Debug Settings - so this build omits
    // them and logs the reason for each.
    static const QHash<QString,QString> omitted{
        {"edy","the Japan-only Edy Viewer: the sysconf plugin gates it and this build has no page for it"},
        {"console_qa","Debug Settings: QA consoles only"}};
    QVector<Entry> rows;
    int skipped=0;
    for(const auto& row:column.rows) {
        const auto key=row.itemKey.isEmpty()?row.queryKey:row.itemKey;
        if(const auto why=omitted.constFind(key);why!=omitted.cend()) {
            qInfo().noquote()<<"xmbml: settings column omits"<<key<<"("<<*why<<")";
            ++skipped;continue;
        }
        if(row.title.isEmpty()) {
            qWarning().noquote()<<"xmbml: settings row"<<key<<"has no title (no title_rsc, no fallback)";
            ++skipped;continue;
        }
        const auto found=byId.constFind(row.id);
        Entry entry=found==byId.cend()?Entry{}:*found;
        entry.id=row.id;
        entry.title=row.title;
        // fo_lbl2_alpha=0 on the three child=segment folder rows: a folder is not a setting, so its row
        // carries no second label line. Every other row keeps the definition's info_rsc (D3).
        entry.detail=row.hasSecondLabel()?row.detail:QString{};
        // The definition's own icon (tex_* in explore_category_sysconf). The qgl/icontex normal-map
        // override that used to replace every settings icon is gone (D1).
        if(!row.icon.isEmpty())entry.texture=row.icon;
        entry.container=column.container;
        entry.source=fieldSourceName(row.titleSource);
        if(found==byId.cend()) {
            // A firmware row with no page in this build: listed and inert rather than dropped.
            entry.action=Action::Unavailable;entry.setting.clear();entry.value.clear();
            qWarning().noquote()<<"xmbml: settings row"<<row.id<<"has no native page; listed as inert";
        }
        if(verbose)qInfo().noquote()<<"xmbml: settings row"<<entry.id<<"key="+key
            <<"title=\""+entry.title+"\""<<"texture="+entry.texture<<"source="+entry.source
            <<"segment="+(row.segment()?QStringLiteral("true"):QStringLiteral("false"))
            <<"secondLabel="+(row.hasSecondLabel()?QStringLiteral("true"):QStringLiteral("false"))
            <<"barAction="+(row.barAction.isEmpty()?QStringLiteral("-"):row.barAction);
        rows.push_back(std::move(entry));
    }
    if(rows.isEmpty()) {
        qWarning().noquote()<<"xmbml: settings column has no resolvable row; keeping the built-in rows";
        return {};
    }
    qInfo().noquote()<<QStringLiteral("xmbml: settings column resolved %1 rows from %2 (queries %3, "
        "expanded %4, sha256 %5): listed %6, omitted %7, rco titles %8 in %9 ms")
        .arg(int(rows.size())).arg(QFileInfo(column.path).fileName()).arg(column.queries).arg(column.expanded)
        .arg(column.sha256.left(12)).arg(int(rows.size())).arg(skipped).arg(column.rcoRows).arg(timer.elapsed());
    return rows;
}

void Shell::openSettings(const QString& id) {
    if(pages_.size()>=32)return;
    const auto tr=[&](const QString& key,const QString& fallback){return assets_.text("sysconf_plugin",key,fallback);};
    // Phase 11.3: the current value of a choice row's preference, for the isCurrent mark of its list.
    // The same read the openChoices path does, including the two values that are read from the computer
    // rather than stored (host volume/mute) and the media repeat mode, so a list's mark and the row's
    // value text cannot disagree.
    const std::function<QVariant(const QString&)> currentFor=[this](const QString& key)->QVariant {
        if(key=="host/volume")return int(std::lround(host::masterVolume()/10.)*10);
        if(key=="host/muted")return host::masterMuted();
        if(key=="media/repeatMode")return mediaRepeatMode();
        return preferences_.value(key);
    };
    Page page;
    // The time zone diagnostic (SETTINGS 14): XMB_TZ_SELFCHECK=<dir> runs it once, on the first settings
    // page any scripted or manual run opens, and writes <dir>/tz-selfcheck.json. It stores a firmware zone
    // token through this shell's own Preferences, reloads the file, and reports the band's current row and
    // the row's value text for that token and for an IANA id; the store is restored before it returns.
    if(qEnvironmentVariableIsSet("XMB_TZ_SELFCHECK")) {
        static bool ran=false;
        if(!ran) {
            ran=true;
            runTimeZoneSelfCheck(assets_,preferences_,[this](const Entry& entry){return valueText(entry);},
                [this]{return clockText();},qEnvironmentVariable("XMB_TZ_SELFCHECK"));
        }
    }
    if(id=="update") { // System Update (software_update_plugin). Not in sysconf_plugin.rco: SETTINGS 5.16/U8.
        const auto su=[&](const QString& key,const QString& fallback){return assets_.text("software_update_plugin",key,fallback);};
        page.title=tr("msg_update","System Update");
        page.entries={option("net",su("msg_update_net","Update via Internet"),{},{},Action::Information,
                su("msg_update_net","Update via Internet")+"\n\nThe native XMB is updated with its own repository; there is no PS3 system software to download."),
            option("media",su("msg_update_media","Update via Storage Media"),{},{},Action::Unavailable)};
    } else if(id=="photo") { // This build's Photo Settings page: the sysconf inventory has no such page.
        page.title="Photo Settings";
        page.entries={option("interval","Slideshow Interval","Sets how long each photo remains on screen.","photo/interval"),
            option("fit","Display Mode","Fits the whole photo or fills the display area.","photo/fit"),
            option("repeat","Repeat","Restarts a slideshow after the final photo.","media/repeat")};
    } else if(id=="messages") { // Friends > Message Box (category_friend.xml)
        const auto ft=[&](const QString& key,const QString& fallback){return assets_.text("explore_category_friend",key,fallback);};
        const auto none=ft("msg_error_no_msg","There are no messages.");
        page.title=ft("msg_msg_box","Message Box");
        auto received=option("received",ft("msg_received_box","Received"),{},{},Action::Information,none);received.container="explore_category_friend";received.texture="item_tex_InBox";received.themeIcon="icon_mbox_received";
        auto sent=option("sent",ft("msg_sent_box","Sent"),{},{},Action::Information,none);sent.container="explore_category_friend";sent.texture="item_tex_OutBox";sent.themeIcon="icon_mbox_sent";
        auto create=option("create",ft("msg_new_msg","Create Message"),{},{},Action::Unavailable);create.container="explore_category_friend";create.texture="item_tex_NewMessage";create.themeIcon="icon_mbox_create";
        page.entries={received,sent,create};
    } else {
        // Every other settings page: the firmware's row set (SETTINGS 5.2-5.16), plus the three segment
        // pages whose rows come from seg_game/seg_device/seg_network.
        const auto dateTimeInfo=[this] {
            const auto value=clockDateTime().toString(clockFullDate(preferences_.value("clock/dateFormat").toInt())
                +" "+clockFullTime(preferences_.value("clock/hourFormat").toInt()));
            return QStringLiteral("Date and Time\n\n")+value+
                QStringLiteral("\n\nThe clock follows the host system. Display format and time zone can be changed within XMB.");
        };
        auto data=pageRows(id,assets_,dateTimeInfo());
        if(data.entries.isEmpty())return; // an id with no page: the caller's row opens nothing
        // Opening the Date and Time page reports the time zone row's current row once per stored value
        // (markCurrentRow): a stored firmware token is one of the band's 110 items, an IANA id is none of
        // them and is not guessed into a row. The band reads the same list, so this is the same verdict,
        // made visible in the log of a run that never writes a dump (SETTINGS 13.6a/14).
        if(id=="date") {
            const auto lists=settingsChoiceLists(QStringLiteral("date"),assets_,currentFor);
            if(lists.isEmpty())qWarning().noquote()<<"settings: the Date and Time choice rows did not resolve";
        }
        page.title=data.title;
        // Dump before the rows move into the page, so the file is what the page will show. The aggregate
        // catalogue is written once per process: it is the whole choice model, so opening any page records
        // every page's lists (audit/SETTINGS-FIRMWARE.md 11).
        dumpChoiceCatalogue(assets_,currentFor);
        dumpSettingsPage(data,assets_,currentFor);
        page.entries=std::move(data.entries);
    }
    pages_.push_back(std::move(page));
}
void Shell::openChoices(const QString& key,const QString& title) {
    if(pages_.size()>=32)return;
    Page page;page.title=title;
    // host/ keys are read from the computer each time (another app may have changed them), never stored.
    const QVariant current=key=="host/volume"?QVariant(int(std::lround(host::masterVolume()/10.)*10)):
        key=="host/muted"?QVariant(host::masterMuted()):key=="media/repeatMode"?QVariant(mediaRepeatMode()):preferences_.value(key);
    for(const auto& choice:choices(key,assets_)) {
        auto entry=option(key+"/"+QString::number(page.entries.size()),choice.title,{},key,Action::SetSetting);
        entry.option=choice.value;
        if(current==choice.value)page.selection=int(page.entries.size());
        page.entries.push_back(std::move(entry));
    }
    if(key=="appearance/theme" && !theme_.path.isEmpty()) {
        auto entry=option(key+"/"+QString::number(page.entries.size()),theme_.name.isEmpty()?QFileInfo(theme_.path).completeBaseName():theme_.name,{},key,Action::SetSetting);
        entry.option=theme_.path;page.selection=int(page.entries.size());page.entries.push_back(std::move(entry));
    }
    pages_.push_back(std::move(page));
}
QString Shell::valueText(const Entry& entry) const {
    if(entry.setting=="media/repeatMode")return QStringList{"Off","Repeat One","Repeat All"}.value(mediaRepeatMode());
    if(entry.action==Action::SelectUser)return entry.value==profiles_.activeId()?QString(QChar(0x25cf)):QString{};
    if(entry.setting.isEmpty())return {};
    const auto value=preferences_.value(entry.setting);
    if(entry.action==Action::SetSetting)return value==entry.option?QString(QChar(0x25cf)):QString{};
    if(entry.setting=="system/name")return value.toString();
    if(entry.setting=="appearance/theme" && !theme_.path.isEmpty())return theme_.name.isEmpty()?QFileInfo(theme_.path).completeBaseName():theme_.name;
    // Phase 11.3: a time zone stored as a firmware zone token is not in choices()'s host-IANA list, and its
    // label is the RCO's own - the same string the band's MItem draws - so the row shows exactly the text
    // its list drew and the band's focus rule finds that item by it (SETTINGS 13.4). A token this firmware
    // does not ship has no label and shows nothing; anything else falls through to the host list below and
    // displays the IANA id it already displayed.
    if(entry.setting=="clock/timeZone" && Preferences::isFirmwareZoneToken(value.toString())) {
        const auto firmware=firmwareZoneFor(assets_,value.toString());
        if(firmware.known)return firmware.label;
        static QSet<QString> logged;
        if(!logged.contains(value.toString())) {
            logged.insert(value.toString());
            qWarning().noquote()<<QStringLiteral("clock: time zone %1 is not a zone this firmware ships; "
                "the row shows no value").arg(value.toString());
        }
        return {};
    }
    for(const auto& choice:choices(entry.setting,assets_))if(choice.value==value)return choice.title;
    return {};
}
QString Shell::fontFamily()const{return assets_.fontFamily(preferences_.value("appearance/font").toInt());}
QDateTime Shell::clockDateTime()const {
    const auto zone=preferences_.value("clock/timeZone").toString();
    auto now=clockOverride_.isValid()?clockOverride_:QDateTime::currentDateTime();
    // The three stored forms (preferences.h): "System" (the default) and the empty value leave the host
    // clock alone; an IANA id resolves through QTimeZone exactly as it always has, which is why no stored
    // value was migrated; a firmware zone token resolves through the item's own "GMT+HH:MM" label, because
    // the firmware's 110 zones are offset zones with a city label, not IANA identities. A token this
    // firmware does not ship resolves to no zone at all - the clock follows the host and says so once -
    // rather than to a guessed offset.
    if(!zone.isEmpty() && zone!=QLatin1String("System")) {
        const QTimeZone named(zone.toUtf8());
        if(named.isValid())now=now.toTimeZone(named);
        else {
            const auto firmware=firmwareZoneFor(assets_,zone);
            if(firmware.known) {
                const auto fixed=QTimeZone::fromSecondsAheadOfUtc(firmware.offsetSeconds);
                if(fixed.isValid())now=now.toTimeZone(fixed);
            } else {
                static QSet<QString> logged;
                if(!logged.contains(zone)) {
                    logged.insert(zone);
                    qWarning().noquote()<<QStringLiteral("clock: time zone %1 is neither an IANA id nor a "
                        "firmware zone this build ships; the clock follows the host").arg(zone);
                }
            }
        }
    }
    return now;
}
QString Shell::clockText()const {
    const auto now=clockDateTime();
    const int dateFormat=preferences_.value("clock/dateFormat").toInt();
    const int hourFormat=preferences_.value("clock/hourFormat").toInt();
    // One space between date and time: the reference frame's word gap is 14 px at this 27 px size, which is
    // a single space, not two (audit/TEXT-RENDERER.md section 5, audit/INFO-BAR.md).
    return now.toString(clockDate(dateFormat)+" "+clockTime(hourFormat));
}
void Shell::chooseWallpaper(const QString& file) {
    if(file.isEmpty())return;
    QImageReader reader(file);
    if(!reader.canRead() || !reader.size().isValid()){message_="This image cannot be used as wallpaper.";emit changed();return;}
    preferences_.setValues({{"appearance/wallpaper",QFileInfo(file).absoluteFilePath()},{"appearance/background",2}});emit changed();
}
void Shell::chooseTheme(const QString& file) {
    if(file.isEmpty())return;
    QString error;
    if(P3t::load(file,&error).isNull()){message_=QFileInfo(file).fileName()+"\n\n"+error;emit changed();return;}
    // The theme's still background is shown through the wallpaper mode (View::loadWallpaper puts it before the user's own).
    preferences_.setValues({{"appearance/theme",QFileInfo(file).absoluteFilePath()},{"appearance/background",2}});emit changed();
}
void Shell::setSystemName(const QString& name){preferences_.setValue("system/name",name.trimmed());emit changed();}
void Shell::setFullscreen(bool enabled){preferences_.setValue("display/fullscreen",enabled);emit changed();}
}
