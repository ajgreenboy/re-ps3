#include "save_data.h"
#include "assets.h"
#include "shell.h"

namespace xmb {
namespace {
struct Utility { const char* kind; const char* title; const char* fallback; const char* texture; const char* container; };
// category_game.xml #seg_mcutility (305-319), #seg_sdps2 (321-338), #seg_sdps3
// (340-357), #seg_sdpsp (371-388). PS1 saves belong to Memory Card Utility, not a fifth row.
constexpr Utility utilities[]{
    {"memory","msg_memory_card_utility","Memory Card Utility","item_tex_ps12util","explore_plugin_full"},
    {"ps2","msg_savedata_utility_ps2","Saved Data Utility (PS2)","item_tex_ps3util","explore_plugin_full"},
    {"ps3","msg_savedata_utility_ps3","Saved Data Utility (PS3)","item_tex_ps3util","explore_plugin_full"},
    {"psp","msg_savedata_utility_minis_psp","Saved Data Utility (minis/PSP)","item_tex_psputil","explore_category_game"}};
const Utility& utility(const QString& kind) {
    for(const auto& spec:utilities)if(kind==QLatin1String(spec.kind))return spec;
    return utilities[2];
}
}
QString saveDataTitle(Assets& assets,const QString& kind) {
    const auto& spec=utility(kind);
    return assets.text("explore_category_game",spec.title,QString::fromUtf8(spec.fallback));
}
QVector<Entry> saveDataRows(Assets& assets,const QString& kind) {
    const auto& spec=utility(kind);
    const QString plugin=kind==QLatin1String("memory")?QStringLiteral("vmc_savedata_plugin"):QStringLiteral("ps3_savedata_plugin");
    const QString empty=assets.text(plugin,"msg_error_no_savedata",
        assets.text("explore_category_game","msg_error_no_savedata","There is no saved data."));
    const QString reason=QObject::tr("No save data source is connected to this native recreation. "
        "PS3, PS2, PSP and internal memory card storage are not implemented. "
        "No saved data can be copied, deleted or inspected on this system.");
    QVector<Entry> rows;
    const auto add=[&](const QString& id,const QString& title,const QString& detail) {
        Entry row;
        row.id=QStringLiteral("savedata:")+kind+QLatin1Char(':')+id;
        row.title=title;row.detail=detail;
        row.container=QString::fromLatin1(spec.container);row.texture=QString::fromLatin1(spec.texture);
        row.action=Action::Unavailable;row.disabled=true;row.source=QStringLiteral("rco");
        rows.push_back(std::move(row));
    };
    // No fabricated ICON0 tiles, USB devices, cloud storage, counts or metadata. The first row is an
    // empty-state notice, not a save. Commands remain reachable only to explain the absent backend.
    // The firmware's list is provider-built; these command rows are a native empty-state affordance,
    // not a claim to reproduce the unobserved runtime list's exact geometry.
    add("empty",empty,reason);
    add("copy",assets.text("ps3_savedata_plugin","msg_copy","Copy"),reason);
    add("delete",assets.text("ps3_savedata_plugin","msg_del","Delete"),reason);
    add("information",assets.text("explore_category_game","msg_info","Information"),reason);
    return rows;
}
void Shell::openSaveData(const QString& kind) {
    Page page;page.title=saveDataTitle(assets_,kind);
    page.entries=saveDataRows(assets_,kind);
    pages_.push_back(std::move(page));
    emit changed();
}
}
