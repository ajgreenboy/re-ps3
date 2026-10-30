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
#include <QCryptographicHash>
#include <QCoreApplication>

namespace xmb {
namespace {
struct Choice {QString title;QVariant value;};
// --- Phase 11.4: the Theme page's run-time lists (audit/SETTINGS-FIRMWARE.md 16) -------------------
// All four Theme pages are empty Page objects in sysconf_plugin.rco (SETTINGS 12.4, 15.1), so the plugin
// builds their contents at run time and the RCO carries no labels for them. The 2026-10-28 capture is the
// only authority for the contents (out/rpcs3-ref2-2026-10-28/lists/):
//   Theme       Install / Original / Classic / Air Paint           (already modelled, unchanged)
//   Color       `Original` text row + 12 unlabelled colour chips   (one 40 px grid, row 0 text)
//   Background  `Brightness` (submenu) / Original / Classic / Wallpaper
//   Font        Original / Rounded / Pop
// Color's twelve chips are the month palettes, in month order: the measured chip colours equal
// qgl/lines' textures/month_bg/rgb/01..12 day palettes, which are the textures View::renderBackground
// uses for `appearance/colour` = Original (the month follows the date). Five months have a value in this
// build's tint table (View::renderBackground's colours[] and fixedMonth[]: 1 Blue is August, 2 Purple is
// June, 3 Red is December, 4 Green is May, 5 Silver is January); the other seven are drawn and disabled
// with the reason, never mapped to a neighbouring tint. `valueName` is what the row's value column says
// for the value the item writes; a chip has no row label at all (the capture draws a swatch only).
constexpr quint32 chipRgb(quint8 r,quint8 g,quint8 b) {
    return (quint32(r)<<16)|(quint32(g)<<8)|quint32(b);
}
// A disabled item's value: outside every bounded key, so Preferences refuses the commit (an RCO list's
// inert items keep their ordinal; the Theme lists' real values are 0..5, so they need another range).
constexpr int kInertValue=1000;
struct ThemeItemSpec {
    const char* label=nullptr;      // the row's own text; empty for an unlabelled colour chip
    const char* valueName=nullptr;  // the value column's name for this value (empty when !writes)
    int value=0;                    // the stored value (ignored when !writes)
    bool writes=false;
    quint32 colour=0;               // the swatch measured on the capture, 0xRRGGBB (0: no swatch)
    bool submenu=false;             // the firmware's right-hand chevron row (`Brightness`)
    const char* note=nullptr;       // why the item cannot be committed (empty when it writes)
    // A file picker the console folds into this list rather than drawing as a page row (SETTINGS 5.6/D11: the
    // Theme page is Theme/Color/Background/Font and nothing else). `picker` is the sentinel value the item
    // carries: no bounded preferences key accepts it, and Shell::openChoices turns it into an Action::Theme /
    // Action::Wallpaper row instead of a write, so Cross opens the file dialog.
    const char* picker=nullptr;
};
// The Theme list's `Install` item and the Background list's wallpaper item carry these sentinels
// (kThemePickerValue / kWallpaperPickerValue, settings_menu.h): activate() opens the file dialog for them.
const char* kMonthNote="this build has no theme tint for this month's palette; its six tints are Original, "
    "Blue=August, Purple=June, Red=December, Green=May, Silver=January";
const QVector<ThemeItemSpec>& themeColourSpec() {
    // Chip colours sampled at x 1355, chip top + 14 on out/rpcs3-ref2-2026-10-28/lists/color-list-rest.png
    // (tops 531 + 40n; the 12th chip is revealed by the last scroll, -down12/14). The other seven months
    // are disabled because the tint table has no value for them, not because the chip is doubtful.
    static const QVector<ThemeItemSpec> table{
        {"Original","Original",0,true,0,false,nullptr},
        {"","Silver",5,true,chipRgb(184,191,202),false,nullptr},                 // January
        {"","",0,false,chipRgb(204,184,28),false,kMonthNote},                    // February
        {"","",0,false,chipRgb(115,169,40),false,kMonthNote},                    // March
        {"","",0,false,chipRgb(222,110,129),false,kMonthNote},                   // April
        {"","Green",4,true,chipRgb(29,123,29),false,nullptr},                    // May
        {"","Purple",2,true,chipRgb(146,111,189),false,nullptr},                 // June
        {"","",0,false,chipRgb(36,184,171),false,kMonthNote},                    // July
        {"","Blue",1,true,chipRgb(8,55,166),false,nullptr},                      // August
        {"","",0,false,chipRgb(141,53,155),false,kMonthNote},                    // September
        {"","",0,false,chipRgb(195,144,17),false,kMonthNote},                    // October
        {"","",0,false,chipRgb(100,67,23),false,kMonthNote},                     // November
        {"","Red",3,true,chipRgb(184,53,36),false,nullptr}};                     // December
    return table;
}
const QVector<ThemeItemSpec>& themeBackgroundSpec() {
    // lists/background-list-rest.png: `Brightness` at ink top 381 with a right-pointing chevron measured at
    // x 1786-1796, y 387-396 (a submenu row), then Original/Classic/Wallpaper at 541/581/621.
    static const QVector<ThemeItemSpec> table{
        {"Brightness","",0,false,0,true,
            "the firmware opens a second level here; this build has no background-brightness control "
            "(Preferences holds no such key) and the second level's contents were not captured (SETTINGS "
            "12.4c/12.8), so the row is shown where the firmware puts it and cannot be committed"},
        {"Original","Original",0,true,0,false,nullptr},
        {"Classic","Classic",1,true,0,false,nullptr},
        {"Wallpaper","Wallpaper",2,true,0,false,nullptr},
        // The wallpaper picker, moved here from the Theme page's own row list. On the console the background's
        // Wallpaper item IS the way a user wallpaper is chosen (the page has no picker row of its own), so this
        // is where the console's structure puts it; the item is last so the captured four-item block keeps its
        // measured order and origins (SETTINGS 12.4c: Brightness/Original/Classic/Wallpaper at 381/537/581/621).
        {"Select Wallpaper","",0,false,0,false,
            nullptr,kWallpaperPickerValue}};
    return table;
}
const QVector<ThemeItemSpec>& themeFontSpec() {
    // lists/font-list-rest.png: Original / Rounded / Pop. All three are applied: appearance/font 0..2
    // selects Assets' registered family. The name-to-file mapping is F (THEMES 6): SCE-PS3 Rodin LATIN is
    // 0, SCE-PS3 Seurat LATIN is 1 and VAGRundschriftDLig is 2, so Rounded writes 2 and Pop writes 1.
    static const QVector<ThemeItemSpec> table{
        {"Original","Original",0,true,0,false,nullptr},
        {"Rounded","Rounded",2,true,0,false,nullptr},
        {"Pop","Pop",1,true,0,false,nullptr}};
    return table;
}
// The Theme lists' Choice form, for the column model (Shell::openChoices) and the value column
// (Shell::valueText): a disabled item is present, so it can take the focus, with a sentinel value that no
// bounded key accepts. One table drives both halves, so the band and the commit path cannot disagree.
QVector<Choice> themeChoices(const QVector<ThemeItemSpec>& spec) {
    QVector<Choice> result;
    int index=0;
    for(const auto& item:spec) {
        const char* title=item.valueName?item.valueName:item.label;
        // A picker item carries its sentinel so the page and the list agree on which row it is; openChoices
        // makes it an action row instead of a write.
        const QVariant value=item.picker?QVariant(QString::fromLatin1(item.picker))
            :item.writes?QVariant(item.value):QVariant(kInertValue+index);
        result.push_back({title?QString::fromLatin1(title):QString{},value});
        ++index;
    }
    return result;
}
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
    // Phase 11.4: the three run-time Theme lists, in the console's authored order (SETTINGS 16). Color's
    // items carry no row label; the six values this build can store keep the names the value column shows
    // (Original, Silver, Green, Purple, Blue, Red), and every item is present so the focus can reach it - a
    // disabled item's option is a sentinel Preferences refuses (kInertValue).
    if(key=="appearance/colour")return themeChoices(themeColourSpec());
    // The Theme list's `Install` is the .p3t picker: the console's own first item (SETTINGS 12.4a) and the only
    // way to load a theme file since the page's own picker row was removed. Its sentinel value makes
    // openChoices() build an Action::Theme row; the current theme joins the list below in openChoices.
    if(key=="appearance/theme")return {{QString::fromLatin1("Install"),QString::fromLatin1(kThemePickerValue)},
        {text("msg_theme_default","Original"),QString{}}};
    if(key=="appearance/background")return themeChoices(themeBackgroundSpec());
    if(key=="appearance/font")return themeChoices(themeFontSpec());
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
    // The four console keyboard/mouse rows the Accessory page writes for real (host::setHostChoice). The
    // labels are sysconf_plugin's own item texts: Short/Normal/Long, Fast/Normal/Slow, Right-/Left-Handed.
    if(key=="host/keyRepeatDelay")return {{"Short",0},{"Normal",1},{"Long",2}};
    if(key=="host/keyRepeatSpeed")return {{"Fast",0},{"Normal",1},{"Slow",2}};
    if(key=="host/mouseButtons")return {{"Right-Handed",0},{"Left-Handed",1}};
    if(key=="host/pointerSpeed")return {{"Fast",0},{"Normal",1},{"Slow",2}};
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
Entry rowEntry(const RowSpec& spec,Assets& assets,const QString& pageId) {
    const auto text=[&assets](const char* key){return key?assets.text("sysconf_plugin",QString::fromLatin1(key)):QString{};};
    const auto title=text(spec.titleKey);
    const auto detail=text(spec.infoKey);
    const auto literalTitle=spec.literalTitle?QString::fromLatin1(spec.literalTitle):QString{};
    const auto literalDetail=spec.literalDetail?QString::fromLatin1(spec.literalDetail):QString{};
    Entry entry=option(QString::fromLatin1(spec.id),title.isEmpty()?literalTitle:title,
        detail.isEmpty()?literalDetail:detail,spec.setting?QString::fromLatin1(spec.setting):QString{},
        spec.action,spec.value?QString::fromLatin1(spec.value):QString{});
    // The page the row belongs to: Shell::valueText and Shell::activate use it to resolve the live value and
    // the information page through host::rowValue / host::rowDetail.
    entry.settingsPage=pageId;
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
        {"autodate","msg_automatic_datetime_setting","msg_automatic_datetime_setting_explanation",Action::Information,nullptr,nullptr,"Set Automatically"},
        {"date_fmt","msg_date_fmt","msg_date_fmt_explanation",Action::SettingChoices,"clock/dateFormat",nullptr,"Date Format"},
        {"time_fmt","msg_time_fmt","msg_time_fmt_explanation",Action::SettingChoices,"clock/hourFormat",nullptr,"Time Format"},
        {"timezone","msg_timezone","msg_timezone_explanation",Action::SettingChoices,"clock/timeZone",nullptr,"Time Zone"},
        {"summertime","msg_summertime","msg_summertime_explanation",Action::Information,nullptr,nullptr,"Daylight Saving"}};
    // System Settings: the console's 25-row order (ref2 tails/system-page-top + system-down1..20, M2 4.6),
    // plus `Install Other OS`, which the RCO carries and the 25-row walk does not draw; it stays where the
    // firmware's own sequence puts it (between Restore PS3 System and System Information). Every row that
    // has no honest host value is an Action::Information row whose page says so (SETTINGS 1.7).
    static const QVector<RowSpec> system{
        {"auto_update","msg_auto_update","msg_auto_update_explanation",Action::Information,nullptr,nullptr,"Automatic Update"},
        {"name","msg_ps3_name","msg_ps3_name_explanation",Action::EditName,"system/name",nullptr,"System Name"},
        {"console_lang","msg_console_lang","msg_console_lang_explanation",Action::Information,nullptr,nullptr,"System Language"},
        {"characterset","msg_characterset","msg_characterset_explanation",Action::Information,nullptr,nullptr,"Character Set"},
        {"dictionary_type","msg_dictionary_type","msg_dictionary_type_explanation",Action::Information,nullptr,nullptr,"Dictionary Type"},
        {"dict_edit","msg_word_register","msg_word_register_explanation",Action::Information,nullptr,nullptr,"Add/Edit Term"},
        {"dict_delete","msg_del_studyhistory","msg_del_studyhistory_explanation",Action::Information,nullptr,nullptr,"Delete Predictive Text Dictionary"},
        {"notification_message","msg_notification_message","msg_notification_message_explanation",Action::Information,nullptr,nullptr,"Notification Messages"},
        {"trophy_notification","msg_trophy_notification","msg_trophy_notification_info",Action::Information,nullptr,nullptr,"Trophy Notifications"},
        {"whats_new","msg_your_channel_display","msg_your_channel_display_explanation",Action::Information,nullptr,nullptr,"Display [What's New]"},
        {"disc_auto_start","msg_disc_auto_start","msg_disc_auto_start_explanation",Action::Information,nullptr,nullptr,"Disc Auto-Start"},
        {"control_hdmi","msg_control_for_hdmi","msg_control_for_hdmi_explanation",Action::Information,nullptr,nullptr,"Control for HDMI"},
        {"divx_code","msg_divx_vod_reg_code","msg_divx_vod_reg_code_explanation",Action::Information,nullptr,nullptr,"DivX VOD Registration Code"},
        {"atrac","msg_atrac_activate","msg_atrac_activate_explanation",Action::Information,nullptr,nullptr,"Enable ATRAC"},
        {"wma","msg_wma_playback_enable","msg_wma_playback_enable_explanation",Action::Information,nullptr,nullptr,"Enable WMA Playback"},
        {"vita_network","msg_settings_psvita_wifi","msg_settings_psvita_wifi_exp",Action::Information,nullptr,nullptr,"Connect PS Vita System Using Network"},
        {"vita_list","msg_settings_reg_psvita_list","msg_settings_reg_psvita_list_exp",Action::Information,nullptr,nullptr,"List of Registered PS Vita Systems"},
        {"vita_backup_delete","msg_delete_backup_data_vita","msg_delete_backup_data_vita_explanation",Action::Information,nullptr,nullptr,"Delete PS Vita System's Backup Files"},
        {"disc_utility","msg_disc_utility","msg_disc_utility_explanation",Action::Information,nullptr,nullptr,"Format Utility"},
        {"backup_utility","msg_backup_utility","msg_backup_utility_explanation",Action::Information,nullptr,nullptr,"Backup Utility"},
        {"data_transfer","msg_data_transfer_utility","msg_data_transfer_utility_explanation",Action::Information,nullptr,nullptr,"Data Transfer Utility"},
        {"init_setting","msg_init_setting","msg_init_setting_explanation",Action::ResetPreferences,nullptr,nullptr,"Restore Default Settings"},
        {"init_console","msg_init_console","msg_init_console_explanation",Action::Information,nullptr,nullptr,"Restore PS3 System"},
        {"ex_os_install","msg_ex_os_install","msg_ex_os_install_explanation",Action::Information,nullptr,nullptr,"Install Other OS"},
        {"information","msg_console_info","msg_console_info_explanation",Action::Information,nullptr,nullptr,"System Information"},
        {"aboutps3","msg_aboutps3","msg_aboutps3_explanation",Action::Information,nullptr,nullptr,"About PlayStation 3"}};
    static const QVector<RowSpec> sound{
        {"sound_output_setting","msg_sound_output_setting","msg_sound_output_setting_explanation",Action::Information,nullptr,"host:audio","Audio Output Settings"},
        {"audio_multi_output","msg_audio_multi_output","msg_audio_multi_output_explanation",Action::Information,nullptr,nullptr,"Audio Multi-Output"},
        {"keytone","msg_sound_key","msg_sound_key_explanation",Action::SettingChoices,"audio/keyTone",nullptr,"Key Tone"}};
    static const QVector<RowSpec> display{
        {"display_output_setting","msg_display_output_setting","msg_display_output_setting_explanation",Action::Information,nullptr,"host:display","Video Output Settings"},
        {"screensaver","msg_screensaver","msg_screensaver_explanation",Action::SettingChoices,"display/screensaverMinutes",nullptr,"Screensaver"},
        {"cross_color_filter","msg_cross_color_filter","msg_cross_color_filter_explanation",Action::Information,nullptr,nullptr,"Cross Color Reduction Filter"},
        {"rgb_full_range","msg_rgb_full_range_hdmi","msg_rgb_full_range_hdmi_explanation",Action::Information,nullptr,nullptr,"RGB Full Range (HDMI)"},
        {"super_white","msg_ycbcr_super_white_hdmi","msg_ycbcr_super_white_hdmi_explanation",Action::Information,nullptr,nullptr,"Y Pb/Cb Pr/Cr Super-White (HDMI)"},
        {"deep_color","msg_deep_color_output_hdmi","msg_deep_color_output_hdmi_explanation",Action::Information,nullptr,nullptr,"Deep Color Output (HDMI)"}};
    static const QVector<RowSpec> security{
        {"pw_change","msg_pw_change","msg_pw_change_explanation",Action::Information,nullptr,nullptr,"Change Password"},
        {"bd_pc","msg_bd_pc","msg_bd_pc_explanation",Action::Information,nullptr,nullptr,"BD - Parental Control"},
        {"bd_dvd_pcregion","msg_bd_dvd_use_pcregion","msg_bd_dvd_use_pcregion_explanation",Action::Information,nullptr,nullptr,"BD/DVD - Parental Control Region Code"},
        {"dvd_pc","msg_dvd_pc","msg_dvd_pc_explanation",Action::Information,nullptr,nullptr,"DVD - Parental Control"},
        {"gameetc_pc","msg_gameetc_pc","msg_gameetc_pc_explanation",Action::Information,nullptr,nullptr,"Parental Control"},
        {"pc_browser","msg_pc_browser","msg_pc_browser_explanation",Action::Information,nullptr,nullptr,"Internet Browser Start Control"}};
    // Remote Play's five rows are the measured ones (capture batch 2026-10-28, SETTINGS 9.2); the
    // registration/list/custom-SSID rows open their own pages in the firmware and have no native action.
    static const QVector<RowSpec> remote{
        {"device_registration","msg_device_registration","msg_device_registration_explanation",Action::Information,nullptr,nullptr,"Register Device"},
        {"device_registration_list","msg_device_registration_list","msg_device_registration_list_explanation",Action::Information,nullptr,nullptr,"Registered Device List"},
        {"custom_connection_setting","msg_custom_connection_setting","msg_custom_connection_setting_explanation",Action::Information,nullptr,nullptr,"Custom Connection Settings"},
        {"remote_start","msg_remote_start","msg_remote_start_explanation",Action::Information,nullptr,nullptr,"Remote Start"},
        {"audio_output_device","msg_audio_output_device","msg_audio_output_device_explanation",Action::Information,nullptr,nullptr,"Audio Output Device"}};
    // Video: the BD/DVD player's 19 rows in the console's order (M2 4.6, the 23-still walk). None of them
    // has a host value (this build plays media files, not discs); each opens a page that carries the row's
    // purpose, the machine's disc-player state and the RCO's own option list.
    static const QVector<RowSpec> video{
        {"bd_menu_lang","msg_bd_menu_lang","msg_bd_menu_lang_explanation",Action::Information,nullptr,nullptr,"BD - Menu Language"},
        {"bd_audio_lang","msg_bd_audio_lang","msg_bd_audio_lang_explanation",Action::Information,nullptr,nullptr,"BD - Audio Language"},
        {"bd_subtitle_lang","msg_bd_subtitle_lang","msg_bd_subtitle_lang_explanation",Action::Information,nullptr,nullptr,"BD - Subtitle Language"},
        {"bd_internet_connection","msg_bd_internet_connection","msg_bd_internet_connection_explanation",Action::Information,nullptr,nullptr,"BD - Internet Connection"},
        {"dvd_menu_lang","msg_dvd_menu_lang","msg_dvd_menu_lang_explanation",Action::Information,nullptr,nullptr,"DVD - Menu Language"},
        {"dvd_audio_lang","msg_dvd_audio_lang","msg_dvd_audio_lang_explanation",Action::Information,nullptr,nullptr,"DVD - Audio Language"},
        {"dvd_subtitle_lang","msg_dvd_subtitle_lang","msg_dvd_subtitle_lang_explanation",Action::Information,nullptr,nullptr,"DVD - Subtitle Language"},
        {"cinema_conversion","msg_cinemamode","msg_cinemamode_explanation",Action::Information,nullptr,nullptr,"BD/DVD - Cinema Conversion"},
        {"dvd_wide_display","msg_dvd_wide","msg_dvd_wide_explanation",Action::Information,nullptr,nullptr,"DVD - Wide Display"},
        {"upscaler","msg_bd_dvd_upconvert","msg_upconvert_explanation",Action::Information,nullptr,nullptr,"BD/DVD - Upscaler"},
        {"video_output_format","msg_video_output_format_hdmi","msg_video_output_format_hdmi_explanation",Action::Information,nullptr,nullptr,"BD/DVD - Video Output Format (HDMI)"},
        {"hdmi_1080_24","msg_bd_video_output_1080_24_hdmi","msg_bd_video_output_1080_24_hdmi_explanation",Action::Information,nullptr,nullptr,"1080p 24 Hz Output (HDMI)"},
        {"dynamic_range_control","msg_bd_dvd_dynamic_range_compression","msg_bd_dvd_dynamic_range_compression_explanation",Action::Information,nullptr,nullptr,"BD/DVD - Dynamic Range Control"},
        {"audio_format_hdmi","msg_bd_audio_output_format_hdmi","msg_bd_audio_output_format_explanation",Action::Information,nullptr,nullptr,"BD/DVD - Audio Output Format (HDMI)"},
        {"audio_format_optical","msg_bd_audio_output_format_optical","msg_bd_audio_output_format_explanation",Action::Information,nullptr,nullptr,"BD - Audio Output Format (Optical Digital)"},
        {"storage_audio_lang","msg_hdd_audio_lang","msg_hdd_audio_lang_explanation",Action::Information,nullptr,nullptr,"System Storage - Audio Language"},
        {"storage_subtitle_lang","msg_hdd_subtitle_lang","msg_hdd_subtitle_lang_explanation",Action::Information,nullptr,nullptr,"System Storage - Subtitle Language"},
        {"sequential_play","msg_sequential_play","msg_sequential_play_explanation",Action::Information,nullptr,nullptr,"Sequential Playback"},
        {"hz_50","msg_50hz_video_output","msg_50hz_video_output_explanation",Action::Information,nullptr,nullptr,"50 Hz Video Output"}};
    static const QVector<RowSpec> music{
        {"import_musiccd","msg_import_musiccd","msg_import_musiccd_explanation",Action::Information,nullptr,nullptr,"Audio CD Import"},
        {"crossfade_play","msg_crossfade_play","msg_crossfade_play_explanation",Action::Information,nullptr,nullptr,"Crossfade Playback"},
        {"output_frequency","msg_output_frequency","msg_output_frequency_explanation",Action::Information,nullptr,nullptr,"Output Frequency"},
        {"bitmapping","msg_bitmapping_sacd","msg_bitmapping_sacd_explanation",Action::Information,nullptr,nullptr,"Bitmapping"},
        {"dnormal","msg_music_dnormal","msg_music_dnormal_explanation",Action::Information,nullptr,nullptr,"Dynamic Normalizer"}};
    static const QVector<RowSpec> print{
        {"printer_selection","msg_printer_selection","msg_printer_selection_explanation",Action::Information,nullptr,nullptr,"Printer Selection"},
        {"printer_manager","msg_printer_manager","msg_printer_manager_explanation",Action::Information,nullptr,nullptr,"Printer Manager"}};
    static const QVector<RowSpec> power{
        {"idle","msg_system_auto_power_off","msg_system_auto_power_off_explanation",Action::SettingChoices,"power/idleMinutes",nullptr,"System Auto-Off"},
        {"controller","msg_controller_auto_power_off","msg_controller_auto_power_off_explanation",Action::Information,nullptr,nullptr,"Controller Auto-Off"}};
    static const QVector<RowSpec> chat{
        {"av_chat_start","msg_av_chat_start","msg_av_chat_start_explanation",Action::Information,nullptr,nullptr,"Voice/Video Chat Start"},
        {"audio_device_setting","msg_audio_device_setting","msg_audio_device_setting_explanation",Action::Information,nullptr,nullptr,"Audio Device Settings"},
        {"camera_device_setting","msg_camera_device_setting","msg_camera_device_setting_explanation",Action::Information,nullptr,nullptr,"Camera Device Settings"}};
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
    // The Theme page previously carried two native-only rows ("Select Theme", "Select Wallpaper") at 820 and
    // 891. The console's page is Theme / Color / Background / Font and stops at 749 (SETTINGS 5.6: the frame
    // "shows exactly these four, in this order"), so both rows are gone from the page. They are not lost: the
    // Theme list's own `Install` item is the .p3t picker and the Background list's new `Select Wallpaper` item
    // is the image picker - the two lists the firmware folds these functions into (D11). See themeCaptureChoices
    // and themeBackgroundSpec.
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
    if(id=="music" || id=="video")return &media;
    // The Sound page's firmware set is three rows (D16 removed the four invented power rows). The host's
    // volume and mute are this build's own settings, so they are listed as native-only rows at the end of
    // the page - the same disposition Video/Music give their media settings - and they write the real
    // Windows endpoint through the host layer.
    static const QVector<RowSpec> sound{
        {"host_volume",nullptr,nullptr,Action::SettingChoices,"host/volume",nullptr,"Host Volume",
            "Sets this computer's output volume through Core Audio, the endpoint the media keys move."},
        {"host_mute",nullptr,nullptr,Action::SettingChoices,"host/muted",nullptr,"Host Mute",
            "Mutes this computer's default output device."}};
    if(id=="sound")return &sound;
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
    // Every row of these three pages has a real disposition: the network status list opens the host's own
    // read-only table (the firmware's first row, SETTINGS 5.15), the accessory page's four console
    // keyboard/mouse rows write the matching Windows settings through the host layer (Short/Normal/Long,
    // Fast/Normal/Slow, Right-/Left-Handed), and every remaining row opens its own information page whose
    // body is the machine's real state (host::rowDetail). No row is a "not available" stub.
    static const QHash<QString,Binding> bindings{
        {"net_netlist",{Action::Information,nullptr,"host:network"}},
        {"net_internet",{Action::Information,nullptr,nullptr}},
        {"net_internet_setting",{Action::Information,nullptr,nullptr}},
        {"net_internet_test",{Action::Information,nullptr,nullptr}},
        {"net_media_server",{Action::Information,nullptr,nullptr}},
        // Game: the PSP/PS2 emulation rows have no target on this computer; each says why on its page.
        {"game_psp_emu_viewmode",{Action::Information,nullptr,nullptr}},
        {"game_psp_emu_3d",{Action::Information,nullptr,nullptr}},
        {"game_psp_emu_adhoc_ch",{Action::Information,nullptr,nullptr}},
        {"game_psp_emu_adhoc_mode",{Action::Information,nullptr,nullptr}},
        {"game_ps_emu_upcon",{Action::Information,nullptr,nullptr}},
        {"game_ps_emu_smooth",{Action::Information,nullptr,nullptr}},
        // Accessory: the four Windows settings the console's own choice lists map onto, written for real.
        {"pd_keyrepeat_start",{Action::SettingChoices,"host/keyRepeatDelay",nullptr}},
        {"pd_keyrepeat_speed",{Action::SettingChoices,"host/keyRepeatSpeed",nullptr}},
        {"pd_mouse_type",{Action::SettingChoices,"host/mouseButtons",nullptr}},
        {"pd_pointer_speed",{Action::SettingChoices,"host/pointerSpeed",nullptr}},
        // Accessory: everything else reports the attached hardware and the platform's own limits.
        {"pd_calibrate_motion_controller",{Action::Information,nullptr,nullptr}},
        {"pd_pad_assign",{Action::Information,nullptr,nullptr}},
        {"pd_pad_vibration",{Action::Information,nullptr,nullptr}},
        {"pd_bdremote_reg",{Action::Information,nullptr,nullptr}},
        {"pd_bt_utility",{Action::Information,nullptr,nullptr}},
        {"pd_camera",{Action::Information,nullptr,nullptr}},
        {"pd_audio_setting",{Action::Information,nullptr,nullptr}},
        {"pd_voice_changer",{Action::Information,nullptr,nullptr}},
        {"pd_keyboard_type",{Action::Information,nullptr,nullptr}},
        {"pd_keyboard_input",{Action::Information,nullptr,nullptr}}};
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
            binding==bindings.cend()?Action::Information:binding->action,
            binding==bindings.cend()?QString{}:QString::fromLatin1(binding->value));
        entry.settingsPage=id;
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
        Entry entry=rowEntry(spec,assets,id);
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
        Entry entry=rowEntry(spec,assets,id);
        data.nativeOnly<<entry.id;
        data.entries.push_back(std::move(entry));
    }
    qInfo().noquote()<<QStringLiteral("settings: %1 page from the RCO row set: %2 rows, %3 native-only")
        .arg(id).arg(int(data.entries.size())).arg(data.nativeOnly.size());
    return data;
}
// System Update: the one page whose rows come from the software-update plugin, not sysconf_plugin (SETTINGS
// 5.16/U8). Its two rows are real dispositions here, so the page is built once and shared by the page and
// the report: no update channel exists in this build, and there is no PS3 system software to install.
PageRows updateRows(Assets& assets) {
    PageRows data;data.id=QStringLiteral("update");
    data.title=assets.text("software_update_plugin",QStringLiteral("msg_update"),QStringLiteral("System Update"));
    const auto su=[&assets](const char* key,const char* fallback){return assets.text("software_update_plugin",
        QString::fromLatin1(key),QString::fromLatin1(fallback));};
    auto net=rowEntry(RowSpec{"net","msg_update_net","msg_update_net_explanation",Action::Information,nullptr,
        nullptr,"Update via Internet",nullptr},assets,data.id);
    net.detail=su("msg_update_net_explanation","Checks for a system software update over the Internet.");
    auto media=rowEntry(RowSpec{"media","msg_update_media","msg_update_media_explanation",Action::Information,nullptr,
        nullptr,"Update via Storage Media",nullptr},assets,data.id);
    media.detail=su("msg_update_media_explanation","Checks a storage device for a system software update file.");
    data.entries={net,media};
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
// the 2026-10-28 capture's (settings/theme-choice.png). `Install` is the .p3t picker - the console's own first
// item - and Original is the system theme; Classic and Air Paint are firmware themes this build has no asset
// for, so they are listed and cannot be committed.
ChoiceList themeCaptureChoices(const QVariant& current) {
    ChoiceList list;
    list.pageId=QStringLiteral("theme");list.rowId=QStringLiteral("theme");list.rowTitle=QStringLiteral("Theme");
    list.setting=QStringLiteral("appearance/theme");list.rcoPage=QStringLiteral("page_theme_config_theme");
    list.source=QStringLiteral("capture");
    list.note=QStringLiteral("page_theme_config_theme is an empty Page in sysconf_plugin.rco (0 children), so the "
        "plugin builds this list at run time; these four labels are the capture batch's "
        "(out/rpcs3-ref-2026-10-28/settings/theme-choice.png). Install is the .p3t picker; Original is the only "
        "installed theme this build ships; Classic and Air Paint ship no asset here.");
    struct Spec {const char* label;bool writes;const char* picker;};
    static const Spec specs[]{{"Install",false,kThemePickerValue},{"Original",true,nullptr},
        {"Classic",false,nullptr},{"Air Paint",false,nullptr}};
    int index=0;
    for(const auto& spec:specs) {
        ChoiceItem item;
        item.label=QString::fromLatin1(spec.label);
        item.source=QStringLiteral("capture");
        item.rcoIndex=index++;
        if(spec.picker) {
            // The picker: a real row (so the band does not dim it and the focus can reach it), carrying the
            // sentinel value Shell::openChoices turns into an Action::Theme row.
            item.value=QString::fromLatin1(spec.picker);
            item.writes=true;
        } else if(spec.writes) {
            item.writes=true;
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
// The Theme page's run-time lists (Color, Background, Font): the capture's contents with this build's
// values. `windowRows` is the number of rows the captured list shows at once (Color: 12). A disabled item
// keeps its measured label/swatch, carries kInertValue+index as its value (so it can take the focus but no
// bounded key accepts a commit) and its reason is logged once per process.
ChoiceList themeRunTimeChoices(const QString& rowId,const QString& rowTitle,const QString& setting,
                               const QVector<ThemeItemSpec>& spec,const QVariant& current,
                               const QString& note,int windowRows) {
    ChoiceList list;
    list.pageId=QStringLiteral("theme");list.rowId=rowId;list.rowTitle=rowTitle;list.setting=setting;
    list.source=QStringLiteral("capture");list.note=note;list.windowRows=windowRows;
    static QSet<QString> logged;
    int index=0;
    for(const auto& theme:spec) {
        ChoiceItem item;
        item.label=theme.label?QString::fromLatin1(theme.label):QString{};
        item.source=QStringLiteral("capture");
        item.rcoIndex=index;
        item.swatch=theme.colour!=0;
        item.colour=theme.colour;
        item.submenu=theme.submenu;
        if(theme.picker) {
            // The wallpaper picker: a real item of the list (the console's own `Wallpaper` item is how a user
            // wallpaper is chosen), carrying the sentinel Shell::openChoices turns into an Action::Wallpaper row.
            item.value=QString::fromLatin1(theme.picker);item.writes=true;
            item.source=QStringLiteral("native");
        } else if(theme.writes) {
            item.value=theme.value;item.writes=true;
            item.isCurrent=choiceValueMatches(current,item.value);
        } else {
            item.value=kInertValue+index;item.writes=false;
            item.note=theme.note?QString::fromLatin1(theme.note):QString{};
            const QString key=setting+QStringLiteral("/%1").arg(index);
            if(!item.note.isEmpty() && !logged.contains(key)) {
                logged.insert(key);
                qWarning().noquote()<<QStringLiteral("settings: %1 item %2 (%3) is disabled: %4")
                    .arg(setting).arg(index)
                    .arg(item.label.isEmpty()?QStringLiteral("unlabelled chip"):item.label)
                    .arg(item.note);
            }
        }
        list.items.push_back(item);
        ++index;
    }
    list.scrollable=windowRows>0?list.items.size()>windowRows:list.items.size()>kBandRows;
    for(auto& item:list.items)item.scrollable=list.scrollable;
    markCurrentRow(list,current);
    return list;
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
    // Phase 11.4: the other three Theme rows. Their pages are empty in the RCO for the same reason, but the
    // capture measured each one's rows, so the list is the capture's (source "capture", the same provenance
    // value the Theme row uses) and the note says what the contents are and where they were measured. The
    // Color list's 12-row window is the measured one; the other two fit.
    if(pageId==QLatin1String("theme") && (entry.id==QLatin1String("color")
        || entry.id==QLatin1String("background") || entry.id==QLatin1String("font"))) {
        const bool colour=entry.id==QLatin1String("color");
        const bool background=entry.id==QLatin1String("background");
        const auto& spec=colour?themeColourSpec():background?themeBackgroundSpec():themeFontSpec();
        const QString note=colour
            ?QStringLiteral("page_theme_config_color is an empty Page in sysconf_plugin.rco (0 children), so the "
                "plugin builds this list at run time. Contents are the 2026-10-28 capture's "
                "(out/rpcs3-ref2-2026-10-28/lists/color-list-rest.png and -down1..14): a first text row `Original` "
                "and twelve unlabelled colour chips on one 40 px grid, 12 rows visible then a down-chevron. The "
                "twelve chip colours equal qgl/lines' textures/month_bg/rgb/01..12 day palettes in order (the "
                "textures View::renderBackground uses for Original), so chip n is month n. Five months write this "
                "build's tint table (view.cpp colours[]/fixedMonth[]): January=Silver 5, May=Green 4, June=Purple "
                "2, August=Blue 1, December=Red 3; the other seven are drawn and disabled with their reason.")
            :background
            ?QStringLiteral("page_theme_config_background is an empty Page in sysconf_plugin.rco (0 children), so "
                "the plugin builds this list at run time. Contents are the 2026-10-28 capture's "
                "(out/rpcs3-ref2-2026-10-28/lists/background-list-rest.png): `Brightness` (a submenu row with the "
                "measured right-hand chevron at x 1786-1796, four 40 px pitches above the value block), then "
                "`Original`, `Classic`, `Wallpaper`. The three values are this build's appearance/background 0/1/2; "
                "`Brightness` is disabled because this build has no background-brightness control and the "
                "firmware's second level was not captured.")
            :QStringLiteral("page_theme_config_font is an empty Page in sysconf_plugin.rco (0 children), so the "
                "plugin builds this list at run time. Contents are the 2026-10-28 capture's "
                "(out/rpcs3-ref2-2026-10-28/lists/font-list-rest.png): `Original`, `Rounded`, `Pop`. All three are "
                "applied through appearance/font 0..2, which selects Assets' registered family - 0 SCE-PS3 Rodin "
                "LATIN, 1 SCE-PS3 Seurat LATIN, 2 VAGRundschriftDLig - so `Rounded` writes 2 and `Pop` writes 1. "
                "Which family is the firmware's `rounded` and which its `pop` is not traced locally (THEMES 6).");
        auto captured=themeRunTimeChoices(entry.id,entry.title,entry.setting,spec,current,note,
            colour?12:0);
        captured.rcoPage=QString::fromLatin1(binding->rcoPage?binding->rcoPage:"");
        captured.container=QStringLiteral("sysconf_plugin");
        captured.rcoPageOffset=page.page?page.page->offset:0;
        captured.scrollObject=page.scrollObject;
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
        {"rcoIndex",item.rcoIndex},{"rcoOffset",qint64(item.rcoOffset)},{"writes",item.writes},
        // Phase 11.4: a Theme colour chip's only identity, and the reason a disabled item cannot commit.
        {"swatch",item.swatch},{"colour",item.swatch?QStringLiteral("#%1").arg(item.colour,6,16,QChar('0')):QString{}},
        {"submenu",item.submenu},{"note",item.note}};
}
QJsonObject choiceListJson(const ChoiceList& list) {
    QJsonArray items;
    for(const auto& item:list.items)items.push_back(choiceItemJson(item));
    return QJsonObject{{"row",list.rowId},{"rowTitle",list.rowTitle},{"setting",list.setting},
        {"rcoPage",list.rcoPage},{"rcoPageOffset",qint64(list.rcoPageOffset)},
        {"container",list.container},{"source",list.source},{"note",list.note},
        {"scrollable",list.scrollable},{"scrollObject",list.scrollObject},{"windowRows",list.windowRows},
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
        // The value the page's value column shows, and where it comes from: the machine layer first, then
        // the app's own store. A row with neither is an action (a picker, a page, a confirmed reset), which
        // is what the console draws too. This is the dump a verification grep can read to prove no settings
        // row shows a placeholder.
        const auto reading=host::rowValue(data.id,entry.id);
        QString display=reading.text;
        QString valueSource=reading.source;
        QString valueStatus=reading.status;
        if(display.isEmpty() && !entry.setting.isEmpty()) {
            const auto current=currentFor?currentFor(entry.setting):QVariant{};
            display=settingsChoiceLabel(entry.setting,current,assets);
            if(!display.isEmpty()) {valueSource=QStringLiteral("app");valueStatus=QStringLiteral("live");}
        }
        if(display.isEmpty())valueStatus=entry.action==Action::Information?QStringLiteral("read-only"):QStringLiteral("action");
        // Two rows carry a live value rather than a stored label, and Shell::valueText resolves both: the Date
        // and Time row's clock (in the two stored formats) and System Name's default (this computer's name
        // before one is set). The dump resolves them the same way, and the report uses the same two rules, so
        // the three cannot disagree (SETTINGS 3.2/3.6).
        if(data.id==QLatin1String("date") && entry.id==QLatin1String("datetime")) {
            const int dateFormat=currentFor?currentFor(QStringLiteral("clock/dateFormat")).toInt():0;
            const int hourFormat=currentFor?currentFor(QStringLiteral("clock/hourFormat")).toInt():1;
            display=QDateTime::currentDateTime().toString(clockFullDate(dateFormat)+" "+clockTime(hourFormat));
            valueSource=QStringLiteral("host:clock");valueStatus=QStringLiteral("live");
        }
        if(entry.setting==QLatin1String("system/name")) {
            const auto stored=currentFor?currentFor(entry.setting).toString().trimmed():QString{};
            display=stored.isEmpty()?host::snapshot().hostName:stored;
            valueSource=stored.isEmpty()?QStringLiteral("host:hostName"):QStringLiteral("pref:system/name");
            valueStatus=QStringLiteral("live");
        }
        // The coverage verdict travels with the dump, so a page capture can be checked without the report.
        const auto coverage=entry.action==Action::Unavailable?QStringLiteral("placeholder"):
            (!display.isEmpty() || entry.action==Action::ResetPreferences)?QStringLiteral("real"):QStringLiteral("placeholder");
        rows.push_back(QJsonObject{{"id",entry.id},{"title",entry.title},{"detail",entry.detail},
            {"action",int(entry.action)},{"setting",entry.setting},{"value",entry.value.left(80)},
            {"displayValue",display},{"valueSource",valueSource},{"valueStatus",valueStatus},
            {"reason",display.isEmpty()?reading.note:QString{}},{"coverage",coverage},
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
QString settingsChoiceLabel(const QString& key,const QVariant& value,Assets& assets) {
    // The host keys read the machine, not the store: the Accessory page's four rows and Sound's volume/mute.
    if(key=="host/volume") {
        const int level=host::snapshot().volume;
        return level<0?QString{}:QString::number(level)+QStringLiteral("%");
    }
    if(key=="host/muted")return host::snapshot().muted?QStringLiteral("On"):QStringLiteral("Off");
    if(key=="host/keyRepeatDelay" || key=="host/keyRepeatSpeed" || key=="host/mouseButtons" || key=="host/pointerSpeed") {
        const int index=host::hostChoiceIndex(key);
        for(const auto& choice:choices(key,assets))if(choice.value==index)return choice.title;
        return {};
    }
    if(key=="media/repeatMode")return QStringList{"Off","Repeat One","Repeat All"}.value(value.toInt());
    if(!value.isValid())return {};
    for(const auto& choice:choices(key,assets))if(choiceValueMatches(value,choice.value))return choice.title;
    return {};
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
        // The definition's second label line is the row's info_rsc, and it is drawn on EVERY column row -
        // including the three child=segment folder rows (C-1 in SETTINGS-COVERAGE 1.1). The frames settle it:
        // col-settings-r01 (Game), r09 (Accessory) and r15 (Network) each draw the info line under the focused
        // folder row, while the previous reading treated the table's fo_lbl2_alpha=0 as "no second label" and
        // suppressed exactly those three. The attribute's real meaning is unattributed; its visible behaviour
        // is not, so the line is drawn whenever the definition carries an info_rsc.
        entry.detail=row.detail;
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

namespace {
// ==== The row's own information page (Phase 11.5) ==================================================
// The (page, row) pairs whose console option list the RCO really carries, with the page object that holds
// it. The labels are read from that page's own MList at run time, so a page shows the firmware's options
// and never this build's wording.
struct OptionPage { const char* page; const char* row; const char* rcoPage; };
const QVector<OptionPage>& rowOptionPages() {
    static const QVector<OptionPage> table{
        // System Settings: page_console_config's own sub-pages.
        {"system","auto_update","page_console_auto_update"},
        {"system","characterset","page_console_characterset"},
        {"system","dictionary_type","page_console_use_dictionary_en"},
        {"system","notification_message","page_console_notification"},
        {"system","trophy_notification","page_console_trophy_notification"},
        {"system","whats_new","page_console_whats_new"},
        {"system","disc_auto_start","page_console_disc_boot_first"},
        {"system","control_hdmi","page_console_control_for_hdmi"},
        {"system","atrac","page_console_atrac_activation"},
        {"system","wma","page_console_wma_activation"},
        {"system","vita_network","page_console_mtpip_setting"},
        // Video Settings: page_bdvd_config's own sub-pages.
        {"video","cinema_conversion","page_bdvd_config_telecine_type"},
        {"video","dvd_wide_display","page_bdvd_config_dvd_wide_screen_type"},
        {"video","upscaler","page_bdvd_config_dvd_up_convert"},
        {"video","video_output_format","page_bdvd_config_bdvd_hdmi_color_space"},
        {"video","hdmi_1080_24","page_bdvd_config_bd_1080_24p"},
        {"video","dynamic_range_control","page_bdvd_config_bdvd_drc"},
        {"video","audio_format_hdmi","page_bdvd_config_bd_sound_format_hdmi"},
        {"video","audio_format_optical","page_bdvd_config_bd_sound_format_spdif"},
        {"video","storage_audio_lang","page_bdvd_config_hdd_audio_lang"},
        {"video","storage_subtitle_lang","page_bdvd_config_hdd_subtitles_lang"},
        {"video","sequential_play","page_bdvd_config_sequential_play"},
        {"video","hz_50","page_bdvd_config_50hz_video_output"}};
    return table;
}
}  // namespace

QString settingsRowBody(const Entry& row,Assets& assets,const QString& renderer,const QString& systemName) {
    QString body;
    const host::Label label=[&assets](const QString& id,const QString& fallback){
        return assets.text("sysconf_plugin",id,fallback);};
    // System Information is the one row whose own page IS the host report, so it is built here rather than
    // composed from the explanation (its payload is empty, and the report must show the same page).
    if(row.settingsPage==QLatin1String("system") && row.id==QLatin1String("information"))
        body=host::systemInformation(renderer,label,systemName);
    if(body.isEmpty())body=host::rowDetail(row.settingsPage,row.id);
    if(body.isEmpty()) {
        // The host layer has no page of its own for this row: its title, the firmware's explanation of what
        // the row does, and the host fact that leaves its value column empty. Nothing else is added.
        const auto reading=host::rowValue(row.settingsPage,row.id);
        QStringList lines{row.title};
        if(!row.detail.isEmpty())lines<<QString{}<<row.detail;
        if(!reading.note.isEmpty())lines<<QString{}<<QStringLiteral("On this computer: ")+reading.note;
        body=lines.join(QLatin1Char('\n'));
    }
    for(const auto& entry:rowOptionPages()) {
        if(row.settingsPage!=QLatin1String(entry.page) || row.id!=QLatin1String(entry.row))continue;
        const auto page=readChoicePage(assets,QStringLiteral("sysconf_plugin"),QString::fromLatin1(entry.rcoPage));
        QStringList labels;
        if(page.container && !page.items.isEmpty()) {
            for(const auto* item:page.items) {
                const auto label=choiceItemLabel(*page.container,*item);
                if(!label.isEmpty())labels<<label;
            }
        }
        if(!labels.isEmpty())
            body+=QStringLiteral("\n\nThe console's own options for this row: ")+labels.join(QStringLiteral(", "))+QLatin1Char('.');
        break;
    }
    return body;
}

namespace {
// What a Cross on a row does, in one word, for the report and the value matrix.
const char* crossName(Action action) {    switch(action) {
    case Action::SettingChoices:return "choices";
    case Action::SetSetting:return "write";
    case Action::Information:return "info";
    case Action::SettingsPage:return "page";
    case Action::EditName:return "editName";
    case Action::CreateUser:return "createUser";
    case Action::SelectUser:return "selectUser";
    case Action::Wallpaper:return "wallpaper";
    case Action::Theme:return "themeFile";
    case Action::ResetPreferences:return "confirmReset";
    case Action::Unavailable:return "none";
    default:return "other";
    }
}
QJsonValue variantJson(const QVariant& value) {
    switch(value.metaType().id()) {
    case QMetaType::Bool:return value.toBool();
    case QMetaType::Int:case QMetaType::LongLong:return double(value.toLongLong());
    case QMetaType::QString:return value.toString();
    default:return value.toString();
    }
}
}

namespace {
// The console inventory this report is graded against (SETTINGS-COVERAGE 2.1/2.2): one entry per page, in
// the column's order, with the number of firmware rows the row matrix counts for it. "column" is the
// Settings column's 16 page entries; "network-status-list" is the level-2 table behind the status row.
// 16 + 114 + 13 = 143.
struct InventoryPage { const char* id; int rows; int level; };
const QVector<InventoryPage>& settingsInventory() {
    static const QVector<InventoryPage> table{
        {"column",16,0},{"update",2,1},{"game",6,1},{"video",19,1},{"music",5,1},{"chat",3,1},{"system",26,1},
        {"theme",4,1},{"date",6,1},{"power",2,1},{"device",14,1},{"print",2,1},{"display",6,1},{"sound",3,1},
        {"security",6,1},{"remote",5,1},{"network",5,1},{"network-status-list",13,2}};
    return table;
}
const InventoryPage* inventoryFor(const QString& id) {
    for(const auto& page:settingsInventory())if(id==QLatin1String(page.id))return &page;
    return nullptr;
}
// A row's coverage verdict, by the matrix's own test (SETTINGS-COVERAGE 0.2). `real` needs a value column
// this computer or this build produced, or a Cross that performs a real, confirm-gated write; every other
// row a page really draws is `placeholder`. `absent` is never returned here: a row with no native row at
// all is counted against the inventory instead, so the report can prove that one is missing.
QString rowCoverage(const Entry& entry,const QString& value) {
    if(entry.action==Action::Unavailable)return QStringLiteral("placeholder");
    if(!value.isEmpty())return QStringLiteral("real");
    if(entry.action==Action::ResetPreferences)return QStringLiteral("real");
    return QStringLiteral("placeholder");
}
QJsonValue nullIfEmpty(const QString& text) {
    return text.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(text);
}
}  // namespace

QJsonObject settingsReport(Assets& assets,Preferences& preferences,const QString& dataDirectory) {
    // A fresh machine read, not the two-second cache: the report is a snapshot of this instant and the
    // backup row must size the directory the caller named.
    host::setDataDirectory(dataDirectory);
    host::invalidateSnapshot();
    const auto machine=host::snapshot();
    const host::Label label=[&assets](const QString& id,const QString& fallback){
        return assets.text("sysconf_plugin",id,fallback);};
    QJsonObject report=host::snapshotJson(machine);   // the whole Snapshot, in its own top-level blocks
    report["schema"]=QStringLiteral("xmb-settings-report/1");
    report["generatedAt"]=QDateTime::currentDateTime().toString(Qt::ISODate);
    report["dataDirectory"]=QDir::toNativeSeparators(dataDirectory);
    report["activeProfile"]=preferences.activeProfile();
    report["profiles"]=QJsonArray::fromStringList(preferences.profileIds());
    report["elevationText"]=host::elevationText();
    report["elevated"]=host::elevated();
    report["privileges"]=QJsonArray::fromStringList(host::privilegeNotes());

    // ---- the build, and the RCO container every label was read from (AT-4) -------------------------
    QJsonObject build{{"version",machine.appVersion},{"qtVersion",machine.qtVersion}};
    {
        QFile executable(QCoreApplication::applicationFilePath());
        if(executable.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if(hash.addData(&executable)) {
                build["sha256"]=QString::fromLatin1(hash.result().toHex());
                build["bytes"]=double(executable.size());
                build["path"]=QDir::toNativeSeparators(executable.fileName());
            }
        }
    }
    report["build"]=build;
    QJsonObject container{{"name",QStringLiteral("sysconf_plugin")}};
    {
        const auto& rco=choiceObjects(assets).container(QStringLiteral("sysconf_plugin"));
        container["path"]=QDir::toNativeSeparators(rco.path);
        container["version"]=int(rco.version);
        container["bytes"]=double(rco.fileBytes);
        container["readable"]=rco.ok;
        if(!rco.error.isEmpty())container["error"]=rco.error;
        QFile file(rco.path);
        if(file.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if(hash.addData(&file))container["sha256"]=QString::fromLatin1(hash.result().toHex());
        }
    }
    report["container"]=container;
    // ---- the choice model's provenance (SETTINGS 0.5/AT-4): every settings list, where it came from and
    // how many items it carries. `noPage` counts the rows whose own page object the RCO does not ship at all.
    {
        int lists=0,items=0,fromRco=0,capture=0,native=0,noPage=0,pageEmpty=0;
        for(const auto& list:settingsChoiceCatalogue(assets)) {
            ++lists;
            items+=list.count();
            if(list.source==QLatin1String("rco"))++fromRco;
            else if(list.source==QLatin1String("capture"))++capture;
            else if(list.source==QLatin1String("native"))++native;
            else if(list.source==QLatin1String("none"))++noPage;
            else ++pageEmpty;
        }
        report["choiceModel"]=QJsonObject{{"lists",lists},{"items",items},{"fromRco",fromRco},
            {"capture",capture},{"native",native},{"noPage",noPage},
            {"emptyPage",pageEmpty},{"bandRows",kBandRows},{"bandPitch",40}};
    }

    // ---- the machine, in the shape SETTINGS 5.1 names, plus the views the acceptance tests name -----
    const auto networkBlock=report.value("network").toObject();
    const auto displayBlock=report.value("display").toObject();
    const auto audioBlock=report.value("audio").toObject();
    const auto accessoryBlock=report.value("accessory").toObject();
    const auto powerBlock=report.value("power").toObject();
    report["host"]=QJsonObject{{"os",machine.osName},{"osVersion",machine.osVersion},{"kernel",machine.kernel},
        {"architecture",machine.architecture},{"cpu",machine.cpuName},{"cpuClock",machine.cpuClock},
        {"cpuCores",machine.cpuCores},{"memoryBytes",double(machine.ramTotal)},
        {"memoryFreeBytes",double(machine.ramFree)},{"gpu",machine.gpuName},{"hostName",machine.hostName},
        {"osBuild",machine.osVersion},{"adapters",networkBlock.value("adapters")},
        {"screens",displayBlock.value("outputs")},{"audio",audioBlock.value("outputs")},
        {"audioInputs",audioBlock.value("inputs")},{"volume",machine.volume},{"muted",machine.muted},
        {"printers",report.value("printers")},{"pads",accessoryBlock.value("pads")},
        {"keyboard",accessoryBlock.value("keyboard")},{"mouse",accessoryBlock.value("mouse")},
        {"powerScheme",machine.powerScheme},{"battery",powerBlock.value("battery")},
        {"clock",report.value("clock")},{"dataDirectory",QDir::toNativeSeparators(machine.dataDirectory)}};
    // AT-15/AT-16: the Sound page's own view of the endpoint.
    QString defaultOutput;
    for(const auto& device:machine.audioOutputs)if(device.isDefault){defaultOutput=device.name;break;}
    report["sound"]=QJsonObject{{"defaultOutput",nullIfEmpty(defaultOutput)},{"volume",machine.volume},
        {"muted",machine.muted},{"outputs",audioBlock.value("outputs")},{"inputs",audioBlock.value("inputs")}};
    // AT-26/AT-27: the two Date rows' host facts.
    const int zoneOffset=machine.clock.utcOffsetSeconds;
    const QString zoneOffsetText=QStringLiteral("GMT%1%2:%3").arg(zoneOffset<0?'-':'+')
        .arg(std::abs(zoneOffset)/3600,2,10,QChar('0')).arg((std::abs(zoneOffset)%3600)/60,2,10,QChar('0'));
    report["date"]=QJsonObject{
        {"autoSync",QJsonObject{{"value",machine.clock.automatic?QStringLiteral("On"):QStringLiteral("Off")},
            {"source","win32"},{"detail",machine.clock.autoDetail}}},
        {"dst",QJsonObject{{"value",machine.clock.daylight?QStringLiteral("Daylight"):QStringLiteral("Standard")},
            {"source","qt"},{"detail",machine.clock.timeZoneId+QStringLiteral(" ")+
                (machine.clock.daylight?machine.clock.daylightName:machine.clock.standardName)}}},
        {"now",machine.clock.now.toString(Qt::ISODate)},{"timeZoneId",machine.clock.timeZoneId},
        {"utcOffset",zoneOffsetText}};
    // AT-14: the display the Video Output row reports.
    QJsonObject currentDisplay;
    for(const auto& display:machine.displays)if(display.primary || currentDisplay.isEmpty()) {
        currentDisplay=QJsonObject{{"width",display.resolution.width()},{"height",display.resolution.height()},
            {"refreshHz",display.refresh},{"primary",display.primary},{"monitor",display.monitor},
            {"adapter",display.adapter},{"bitsPerPixel",display.bitsPerPixel},{"dpi",display.dpi}};
        if(display.primary)break;
    }
    report["display"]=QJsonObject{{"current",currentDisplay},{"outputs",displayBlock.value("outputs")}};
    // AT-18/AT-19/AT-20: the console's 13-row status table, in the console's order, with the host values.
    QJsonObject networkOut=networkBlock;
    {
        QJsonArray list;
        for(const auto& row:host::networkStatusList(label))
            list.push_back(QJsonObject{{"id",row.id},{"label",row.label},{"value",nullIfEmpty(row.value)},
                {"source",row.source},{"present",true},{"note",row.note},
                {"coverage",row.ok?QStringLiteral("real"):QStringLiteral("placeholder")}});
        networkOut["statusList"]=list;
        networkOut["statusListCount"]=list.size();
        networkOut["test"]=host::networkTest(true);
    }
    report["network"]=networkOut;

    // ---- the app's own stored settings, resolved to the labels the pages show ------------------------
    static const QStringList appKeys{"appearance/theme","appearance/colour","appearance/background","appearance/font",
        "appearance/wallpaper","clock/dateFormat","clock/hourFormat","clock/timeZone","audio/volume","audio/keyTone",
        "audio/muted","media/repeatMode","media/repeat","media/shuffle","power/idleMinutes",
        "display/screensaverMinutes","display/fullscreen","system/name","photo/interval","photo/fit"};
    QJsonObject app;
    for(const auto& key:appKeys) {
        const auto value=preferences.value(key);
        auto label2=settingsChoiceLabel(key,value,assets);
        if(label2.isEmpty())label2=value.toString();
        app[key]=QJsonObject{{"value",variantJson(value)},{"label",label2},
            {"stored",preferences.contains(key)},{"shared",Preferences::isSharedKey(key)},
            {"profileKey",preferences.absoluteKey(key)}};
    }
    report["settings"]=app;

    // ---- the Settings column: its 16 page entries, resolved from the firmware's own definition ------
    QStringList pageOrder;
    QJsonArray columnRows;
    {
        XmbmlResolveOptions options;
        options.flashRoot=assets.root();
        options.idAliases={{"console","system"},{"bd_dvd","video"},{"date_time","date"}};
        const auto column=resolveCategoryColumn(QStringLiteral("category_sysconf"),
            [&assets](const QString& containerName,const QString& key){
                const auto value=assets.text(containerName,key);
                if(!value.isEmpty())return value;
                return assets.text("sysconf_plugin",key);
            },options);
        int order=0;
        for(const auto& row:column.rows) {
            // The two rows a retail console does not draw (SETTINGS 0.3) stay out of the inventory too.
            if(row.id==QLatin1String("edy") || row.id==QLatin1String("console_qa"))continue;
            columnRows.push_back(QJsonObject{{"id",row.id},{"title",row.title},{"order",order++},
                {"info",nullIfEmpty(row.detail)},{"firmwareRsc",nullIfEmpty(row.titleRsc)},
                {"firmwareInfoRsc",nullIfEmpty(row.infoRsc)},{"icon",row.icon},
                {"action","page"},{"cross","page"},{"coverage","real"},{"opensPage",row.id},
                {"value",QJsonValue(QJsonValue::Null)},{"valueSource",QJsonValue(QJsonValue::Null)},
                {"secondLabel",!row.detail.isEmpty()}});
            pageOrder<<row.id;
        }
        if(!column.ok())
            qWarning().noquote()<<"settings-report: the column definition did not resolve:"<<column.error<<column.path;
    }
    if(pageOrder.isEmpty())
        pageOrder=QStringList{"update","game","video","music","chat","system","theme","date","power",
            "device","print","display","sound","security","remote","network"};
    report["column"]=QJsonObject{{"title",QStringLiteral("Settings")},{"rows",columnRows},
        {"count",columnRows.size()},{"inventoryRows",16}};
    // The column's own coverage: 16 page entries, each of which opens its page. An entry the definition
    // loses is counted absent, so the inventory total stays exact.
    int columnPresent=int(columnRows.size());

    // ---- every page, every row: the live value, its source, what a Cross does, and its coverage ------
    const QStringList banned{"not available","native recreation","recreation yet","unknown"};
    QStringList placeholderHits;
    const auto scan=[&](const QString& where,const QString& text) {
        for(const auto& word:banned)
            if(text.contains(word,Qt::CaseInsensitive)) {
                placeholderHits<<where+QStringLiteral(": ")+text.left(120);
                return;
            }
    };
    const auto pageBodyFor=[&](const Entry& entry)->QString {
        if(entry.value==QLatin1String("host:network"))return host::networkStatus(label);
        if(entry.value==QLatin1String("host:display"))return host::displayOutput();
        if(entry.value==QLatin1String("host:audio"))return host::audioOutput();
        if(entry.setting==QLatin1String("system/name"))return QString{};
        if(entry.action!=Action::Information)return QString{};
        return settingsRowBody(entry,assets,QString(),preferences.value(QStringLiteral("system/name")).toString());
    };
    QStringList allPages=pageOrder;
    allPages<<QStringLiteral("photo");
    QJsonArray pages;
    int totalRows=0,realRows=0,placeholderRows=0,absentRows=0,withValue=0,writableRows=0,unavailableRows=0;
    int presentRows=0,nativeOnlyRows=0;
    for(const auto& pageId:allPages) {
        PageRows data;
        if(pageId==QLatin1String("update"))data=updateRows(assets);
        else if(pageId==QLatin1String("photo")) {
            data.id=QStringLiteral("photo");data.title=QStringLiteral("Photo Settings");
            data.entries=photoChoiceRows();
            // This build's own page: every row on it is native-only, so none of them enters the inventory.
            for(auto& entry:data.entries){entry.settingsPage=data.id;data.nativeOnly<<entry.id;}
        } else data=pageRows(pageId,assets,QString{});
        if(data.entries.isEmpty()) {
            const auto* inventory=inventoryFor(pageId);
            const int expected=inventory?inventory->rows:0;
            if(expected) {
                qWarning().noquote()<<"settings-report:"<<pageId<<"built no rows;"<<expected<<"inventory row(s) absent";
                absentRows+=expected;
            }
            continue;
        }
        // The row's own firmware resource keys, for AT-2's per-row join.
        QHash<QString,QJsonObject> firmwareByRow;
        for(const auto& row:data.firmware) {
            const auto object=row.toObject();
            firmwareByRow.insert(object.value("id").toString(),object);
        }
        QJsonArray rowArray;
        int order=0,present=0,pageReal=0,pagePlaceholder=0;
        for(const auto& entry:data.entries) {
            const auto reading=host::rowValue(pageId,entry.id);
            QString value=reading.text;
            QString valueSource=value.isEmpty()?QString{}:QStringLiteral("host:")+reading.source;
            QString status=reading.status;
            QString source=reading.source;
            if(pageId==QLatin1String("date") && entry.id==QLatin1String("datetime")) {
                // The Date and Time row carries the running clock as its value (SETTINGS 9.3d), in the
                // formats the two format rows store - the same string Shell::valueText produces.
                const int dateFormat=preferences.value("clock/dateFormat").toInt();
                const int hourFormat=preferences.value("clock/hourFormat").toInt();
                value=QDateTime::currentDateTime().toString(clockFullDate(dateFormat)+" "+clockTime(hourFormat));
                source=QStringLiteral("app");status=QStringLiteral("live");valueSource=QStringLiteral("host:clock");
            }
            if(entry.id==QLatin1String("name")) {
                // AT-8: the stored name wins, and the computer's own name is the default before one is set.
                const auto stored=preferences.value(QStringLiteral("system/name")).toString().trimmed();
                value=stored.isEmpty()?machine.hostName:stored;
                source=stored.isEmpty()?QStringLiteral("qt"):QStringLiteral("app");
                status=QStringLiteral("live");
                valueSource=stored.isEmpty()?QStringLiteral("host:hostName"):QStringLiteral("pref:system/name");
            }
            if(value.isEmpty() && !entry.setting.isEmpty()) {
                const auto current=entry.setting.startsWith(QLatin1String("host/"))?QVariant{}:
                    preferences.value(entry.setting);
                value=settingsChoiceLabel(entry.setting,current,assets);
                if(!value.isEmpty()) {
                    source=QStringLiteral("app");status=QStringLiteral("live");
                    valueSource=QStringLiteral("pref:")+entry.setting;
                }
            }
            if(value.isEmpty() && status.isEmpty())
                status=entry.action==Action::Information?QStringLiteral("read-only"):QStringLiteral("action");
            const auto pageBody=pageBodyFor(entry);
            const auto firmwareRow=firmwareByRow.value(entry.id);
            const auto coverage=rowCoverage(entry,value);
            const bool nativeOnly=data.nativeOnly.contains(entry.id);
            const bool writes=!entry.setting.isEmpty() && entry.action!=Action::Information;
            scan(pageId+QLatin1Char('/')+entry.id+QStringLiteral(" title"),entry.title);
            scan(pageId+QLatin1Char('/')+entry.id+QStringLiteral(" detail"),entry.detail);
            scan(pageId+QLatin1Char('/')+entry.id+QStringLiteral(" value"),value);
            scan(pageId+QLatin1Char('/')+entry.id+QStringLiteral(" page"),pageBody);
            rowArray.push_back(QJsonObject{{"id",entry.id},{"title",entry.title},{"order",order++},
                {"value",nullIfEmpty(value)},{"valueText",nullIfEmpty(pageBody)},
                {"valueSource",nullIfEmpty(valueSource)},{"valueStatus",status},
                {"hostSource",nullIfEmpty(source)},{"note",nullIfEmpty(reading.note)},
                {"reason",value.isEmpty()?nullIfEmpty(reading.note):QJsonValue(QJsonValue::Null)},
                {"action",QString::fromLatin1(crossName(entry.action))},{"actionId",int(entry.action)},
                {"cross",QString::fromLatin1(crossName(entry.action))},
                {"coverage",coverage},
                {"nativeOnly",nativeOnly},{"writable",writes},{"setting",entry.setting},
                {"firmwareRsc",nullIfEmpty(firmwareRow.value("titleRsc").toString())},
                {"firmwareInfoRsc",nullIfEmpty(firmwareRow.value("infoRsc").toString())},
                {"detail",entry.detail}});
            ++totalRows;
            if(!value.isEmpty())++withValue;
            if(writes)++writableRows;
            if(entry.action==Action::Unavailable)++unavailableRows;
            if(nativeOnly){++nativeOnlyRows;continue;}   // a row this build owns is not in the firmware inventory
            ++present;
            if(coverage==QLatin1String("real")){++realRows;++pageReal;}
            else {++placeholderRows;++pagePlaceholder;}
        }
        const auto* inventory=inventoryFor(pageId);
        const int expected=inventory?inventory->rows:0;
        const int absent=inventory?qMax(0,expected-present):0;
        absentRows+=absent;
        presentRows+=present;
        QJsonObject pageObject{{"id",pageId},{"page",pageId},{"title",data.title},{"rows",rowArray},
            {"count",int(data.entries.size())},{"nativeOnly",QJsonArray::fromStringList(data.nativeOnly)},
            {"inventory",QJsonObject{{"expected",expected},{"present",present},{"absent",absent},
                {"level",inventory?inventory->level:0}}},
            {"coverage",QJsonObject{{"real",pageReal},{"placeholder",pagePlaceholder},{"absent",absent}}}};
        if(pageId==QLatin1String("network"))pageObject["statusList"]=networkOut.value("statusList");
        pages.push_back(pageObject);
    }
    report["pages"]=pages;

    // ---- the inventory's own totals: 16 column entries + 114 page rows + 13 status rows = 143 --------
    int inventoryTotal=0;
    QJsonArray inventoryPages;
    for(const auto& page:settingsInventory()) {
        inventoryTotal+=page.rows;
        if(QLatin1String(page.id)==QLatin1String("column"))continue;
        if(QLatin1String(page.id)==QLatin1String("network-status-list"))continue;
        inventoryPages.push_back(QJsonObject{{"id",QString::fromLatin1(page.id)},{"rows",page.rows}});
    }
    // The level-2 table is measured here as well, so its 13 rows carry a value or a reason like every other.
    {
        QJsonArray list;
        int statusReal=0;
        for(const auto& row:host::networkStatusList(label)) {
            if(row.ok)++statusReal;
            list.push_back(QJsonObject{{"id",row.id},{"label",row.label},{"value",nullIfEmpty(row.value)},
                {"source",row.source},{"coverage",row.ok?QStringLiteral("real"):QStringLiteral("placeholder")}});
        }
        realRows+=statusReal;
        placeholderRows+=list.size()-statusReal;
        presentRows+=list.size();
        absentRows+=qMax(0,(inventoryFor(QStringLiteral("network-status-list"))?
            inventoryFor(QStringLiteral("network-status-list"))->rows:0)-list.size());
    }
    // The column's 16 page entries are inventory rows too: each one is real because it opens its page.
    realRows+=columnPresent;
    presentRows+=columnPresent;
    absentRows+=qMax(0,16-columnPresent);
    report["inventory"]=QJsonObject{{"pages",inventoryPages},{"rows",inventoryTotal}};
    report["totals"]=QJsonObject{{"rows",inventoryTotal},{"present",presentRows},{"real",realRows},
        {"placeholder",placeholderRows},{"absent",absentRows},{"withValue",withValue},
        {"writable",writableRows},{"nativeOnlyRows",nativeOnlyRows},
        {"check",realRows+placeholderRows+absentRows}};
    // AT-37: rows whose Cross still raises the generic message. The settings surface has none.
    report["unavailableMessageCount"]=unavailableRows;
    // AT-41: every host write this build performs. The four the matrix permits, then (named separately so
    // the permitted set stays checkable) the Accessory page's four documented per-user input settings.
    report["writeSurface"]=QJsonArray::fromStringList(QStringList{"endpointVolume","endpointMute",
        "displayAwakeHold","displayOffRequest"});
    report["writeSurfaceExtended"]=QJsonArray::fromStringList(QStringList{"keyboardRepeatDelay",
        "keyboardRepeatSpeed","mouseButtonSwap","pointerSpeed"});
    report["writeSurfaceNote"]=QStringLiteral("writeSurface is the four host writes SETTINGS 3.8 permits. "
        "writeSurfaceExtended is the Accessory page's four per-user Windows input settings "
        "(SystemParametersInfo SPI_SETKEYBOARDDELAY/SPEED and SPI_SETMOUSESPEED, SwapMouseButton): no "
        "elevation, no reboot, reversible from the same lists.");
    report["placeholderScan"]=QJsonObject{{"strings",QJsonArray::fromStringList(banned)},
        {"hits",QJsonArray::fromStringList(placeholderHits)}};
    return report;
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
        if(key=="host/keyRepeatDelay" || key=="host/keyRepeatSpeed" || key=="host/mouseButtons" ||
           key=="host/pointerSpeed")return host::hostChoiceIndex(key);
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
        auto data=updateRows(assets_);
        dumpSettingsPage(data,assets_,currentFor);
        page.title=data.title;
        page.entries=std::move(data.entries);
    } else if(id=="photo") { // This build's Photo Settings page: the sysconf inventory has no such page.
        page.title="Photo Settings";
        page.entries={option("interval","Slideshow Interval","Sets how long each photo remains on screen.","photo/interval"),
            option("fit","Display Mode","Fits the whole photo or fills the display area.","photo/fit"),
            option("repeat","Repeat","Restarts a slideshow after the final photo.","media/repeat")};
        for(auto& entry:page.entries)entry.settingsPage=QStringLiteral("photo");
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
        key=="host/muted"?QVariant(host::masterMuted()):
        key.startsWith(QLatin1String("host/"))?QVariant(host::hostChoiceIndex(key)):
        key=="media/repeatMode"?QVariant(mediaRepeatMode()):preferences_.value(key);
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
    // ---- the Settings pages ------------------------------------------------------------------------
    // A row on a Settings page shows the machine's own value first (host::rowValue) and the app's stored
    // value second (settingsChoiceLabel), and either one is a real reading. A row with neither is an action
    // - a picker, an information page or the confirmed reset - which the console also draws with no value
    // (SETTINGS 2.3: Format Utility, Change Password and Remote Play's rows carry none).
    if(!entry.settingsPage.isEmpty()) {
        const auto reading=host::rowValue(entry.settingsPage,entry.id);
        if(!reading.text.isEmpty())return reading.text;
        // The Date and Time row's value is the running clock in the stored formats (SETTINGS 9.3d).
        if(entry.settingsPage==QStringLiteral("date") && entry.id==QStringLiteral("datetime")) {
            const int dateFormat=preferences_.value("clock/dateFormat").toInt();
            const int hourFormat=preferences_.value("clock/hourFormat").toInt();
            return clockDateTime().toString(clockFullDate(dateFormat)+" "+clockTime(hourFormat));
        }
    }
    if(entry.setting.isEmpty())return {};
    const auto value=preferences_.value(entry.setting);
    if(entry.action==Action::SetSetting)return value==entry.option?QString(QChar(0x25cf)):QString{};
    if(entry.setting=="system/name") {
        const auto stored=value.toString().trimmed();
        if(!stored.isEmpty())return stored;
        // SETTINGS 3.2/AT-8: before a name is set the row shows this computer's own name, which is a real
        // reading and the same kind of generated default the console ships ("PS3-354").
        return host::snapshot().hostName;
    }
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
    // The row follows the host zone until the user picks one, so "System" shows which zone that is now:
    // a real reading of this computer rather than a word with no content behind it.
    if(entry.setting=="clock/timeZone" && (value.toString().isEmpty() || value=="System")) {
        const auto zone=host::snapshot().clock;
        if(!zone.timeZoneId.isEmpty())
            return QStringLiteral("System (%1, %2)").arg(zone.timeZoneId).arg(host::snapshot().clock.timeZoneName);
        return QStringLiteral("System");
    }
    // A host-driven key reads the machine, not the store: the volume/mute rows and the four accessory rows.
    if(entry.setting.startsWith(QLatin1String("host/")))
        return settingsChoiceLabel(entry.setting,QVariant{},assets_);
    return settingsChoiceLabel(entry.setting,value,assets_);
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
// Restore Default Settings (System Settings). One atomic write through Preferences::setValues, so a failed
// write leaves the store as it was. The defaults are preferences.cpp's own (defaultValue): this list mirrors
// them because the store exposes no enumerate-defaults call. Libraries, the system name, the profiles and the
// time zone are deliberately kept - the confirmation box says so - because they are identity and content,
// not the appearance and playback settings the console's row resets.
void Shell::restoreDefaultSettings() {
    static const QVector<QPair<QString,QVariant>> defaults{
        {"appearance/colour",0},{"appearance/background",0},{"appearance/font",0},
        {"appearance/theme",QString{}},{"appearance/wallpaper",QString{}},
        {"clock/dateFormat",1},{"clock/hourFormat",1},
        {"audio/volume",60},{"audio/keyTone",true},{"audio/muted",false},
        {"media/repeatMode",0},{"media/repeat",false},{"media/shuffle",false},
        {"power/idleMinutes",0},{"display/screensaverMinutes",0},{"display/fullscreen",false},
        {"photo/interval",5000},{"photo/fit",0}};
    QVariantMap values;
    for(const auto& entry:defaults)values.insert(entry.first,entry.second);
    if(!preferences_.setValues(values)) {
        interactionAudio_.play(InteractionAudio::Cue::Error);
        showMessage(QObject::tr("Restore Default Settings"),
            QObject::tr("The settings could not be written: ")+preferences_.error(),MessageKind::Page);
        return;
    }
    interactionAudio_.play(InteractionAudio::Cue::Accept);
    applyPreferences();
    qInfo().noquote()<<QStringLiteral("settings: Restore Default Settings confirmed; %1 settings reset to "
        "defaults for profile \"%2\" (system name, time zone, libraries and profiles kept)")
        .arg(defaults.size()).arg(preferences_.activeProfile());
    showMessage(QObject::tr("Restore Default Settings"),
        QObject::tr("Every XMB setting has been restored to its default."),MessageKind::Page);
    emit changed();
}
}
