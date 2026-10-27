#include "xmbml_view.h"
#include "catalog.h"
#include <QFileInfo>

namespace xmb {
namespace {
// A Query src points at a view with a '#' fragment and may continue with query fields after it:
// "#seg_manual", "sel://localhost/nopremo?category_network.xml#seg_premo", or
// "xmb://localhost/%flash/xmb/category_network.xml#seg_browser". Everything after '#' is the view.
QString viewFromSource(const QString& src) {
    const int at=src.indexOf(QLatin1Char('#'));
    if(at<0)return {};
    QString view=src.mid(at+1);
    static const QString stops=QStringLiteral("&?/");
    for(const QChar stop:stops) { const int cut=view.indexOf(stop); if(cut>=0)view.truncate(cut); }
    return view.trimmed();
}
// The plugin that produces the node: "sel" (selectors), "ese" (system queries such as the download
// list), "xcb" (content browser), "xmb" (another XMBML file). Empty when the src is a bare "#view".
QString providerFromSource(const QString& src) {
    const int at=src.indexOf(QStringLiteral("://"));
    return at>0?src.left(at).toLower():QString{};
}
}
RowKind XmbmlRow::kind() const {
    if(itemClass==QLatin1String("type:x-xmb/xmlwebbrowser"))return RowKind::WebBrowser;
    if(itemClass==QLatin1String("type:x-xmb/module-action"))return RowKind::ModuleAction;
    if(itemClass==QLatin1String("type:x-xmb/xmlpremo"))return RowKind::RemotePlay;
    if(itemClass==QLatin1String("type:x-xmb/xmlgameexit"))return RowKind::InGameExit;
    // The download queue is the one provider query whose rows this build can produce itself: the
    // firmware names the list by the query's type field (the view defines no item class to read), and
    // the data it lists - which apps are downloading and how far along they are - has a native
    // counterpart in Steam's own manifests (audit/STEAM-DOWNLOADS.md).
    if(provider==QLatin1String("ese") && source.contains(QLatin1String("type=download_control")))
        return RowKind::DownloadControl;
    // No local view and a plugin scheme: the rows exist only after that provider answers a query.
    if(!hasView && !provider.isEmpty())return RowKind::DynamicProvider;
    return RowKind::Unknown;
}
XmbmlColumn readCategoryView(const QString& filePath,const QString& viewId,int rowLimit) {
    XmbmlColumn column;column.path=filePath;column.viewId=viewId;
    if(rowLimit<=0 || rowLimit>64)rowLimit=32;
    const QFileInfo info(filePath);
    if(!info.isFile()){column.error=QStringLiteral("XMBML definition not found: ")+filePath;return column;}
    const auto document=readXmbDocument(filePath);
    column.sha256=document.sha256;
    if(!document.error.isEmpty()){column.error=document.error;return column;}
    const auto root=document.views.constFind(viewId);
    if(root==document.views.constEnd()) {
        column.error=QStringLiteral("view \"")+viewId+QStringLiteral("\" is not defined");
        return column;
    }
    for(const auto& item:root->items) {
        // A category's root view is a list of Query nodes; anything else there is not a column row.
        if(item.kind!=QLatin1String("Query"))continue;
        ++column.queries;
        if(column.rows.size()>=rowLimit)break;
        const auto key=item.attributes.value(QStringLiteral("key"));
        if(key.isEmpty())continue; // a Query without a key has no stable identity to build a row from
        XmbmlRow row;
        row.key=key;
        row.id=key.startsWith(QLatin1String("seg_"))?key.mid(4):key;
        row.source=item.attributes.value(QStringLiteral("src"));
        row.provider=providerFromSource(row.source);
        row.viewId=viewFromSource(row.source);
        row.inGameOnly=row.source.startsWith(QLatin1String("sel://localhost/ingame"));
        const auto view=document.views.constFind(row.viewId);
        if(view!=document.views.constEnd()) {
            row.hasView=true;
            if(!view->items.isEmpty()) {
                row.itemClass=view->items.first().attributes.value(QStringLiteral("class"));
                row.itemKey=view->items.first().attributes.value(QStringLiteral("key"));
            }
            // The view's <Table> is keyed by the item's attr (or its key); the query's own attr names
            // the *parent* view's attribute table (e.g. seg_browser's focus_priority), so the item's
            // attr is tried first. A view with exactly one table is unambiguous.
            QString attr;
            if(!view->items.isEmpty())attr=view->items.first().attributes.value(QStringLiteral("attr"));
            if(attr.isEmpty())attr=item.attributes.value(QStringLiteral("attr"));
            if(attr.isEmpty())attr=row.itemKey;
            auto table=view->tables.constFind(attr);
            if(table==view->tables.constEnd() && view->tables.size()==1)table=view->tables.constBegin();
            if(table!=view->tables.constEnd()) {
                row.hasTable=true;
                row.titleRsc=table->value(QStringLiteral("title_rsc"));
                row.iconRsc=table->value(QStringLiteral("icon_rsc"));
                row.infoRsc=table->value(QStringLiteral("info_rsc"));
                row.moduleName=table->value(QStringLiteral("module_name"));
                row.moduleAction=table->value(QStringLiteral("module_action"));
            }
        }
        column.rows.push_back(row);
    }
    if(column.rows.isEmpty())
        column.error=QStringLiteral("the \"")+viewId+QStringLiteral("\" view defines no Query rows");
    return column;
}
} // namespace xmb
