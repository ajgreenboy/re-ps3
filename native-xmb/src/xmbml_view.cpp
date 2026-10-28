#include "xmbml_view.h"
#include "catalog.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>

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
// The item class decides what the row is; a Query with no local view is a provider row. One place
// for the rule, shared by the query-level XmbmlRow and the resolved XmbmlColumnRow, so the two
// readers cannot drift apart.
RowKind rowKindFor(const QString& itemClass,const QString& provider,const QString& source,bool hasView) {
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
QueryKind queryKindForProvider(const QString& provider) {
    if(provider.isEmpty()||provider==QLatin1String("xmb"))return QueryKind::Local;
    if(provider==QLatin1String("sel"))return QueryKind::Selector;
    if(provider==QLatin1String("ese"))return QueryKind::SystemQuery;
    if(provider==QLatin1String("xcb"))return QueryKind::ContentBrowser;
    if(provider==QLatin1String("xil"))return QueryKind::ContentList;
    if(provider==QLatin1String("user"))return QueryKind::UserProvider;
    if(provider==QLatin1String("now"))return QueryKind::NowPlaying;
    if(provider==QLatin1String("chat"))return QueryKind::Chat;
    return QueryKind::Unknown;
}
// "vsh/resource/explore/xmb/category_sysconf.xml" -> "sysconf": the column's own name and the
// suffix of its RCO container (explore_category_sysconf).
QString categoryStem(const QString& path) {
    QString name=QFileInfo(path).fileName();
    if(name.endsWith(QLatin1String(".xml"),Qt::CaseInsensitive))name.chop(4);
    if(name.startsWith(QLatin1String("category_")))name=name.mid(9);
    return name;
}
// A firmware key's native id: the item key when the row came from a view, else the query key
// without the firmware's "seg_" namespace. The aliases let the caller keep the ids it already uses.
QString nativeId(const QString& key,const QHash<QString,QString>& aliases) {
    QString candidate=key;
    if(candidate.startsWith(QLatin1String("seg_")) && candidate.size()>4)candidate=candidate.mid(4);
    const auto alias=aliases.constFind(key);
    if(alias!=aliases.cend() && !alias->isEmpty())return *alias;
    const auto candidateAlias=aliases.constFind(candidate);
    if(candidateAlias!=aliases.cend() && !candidateAlias->isEmpty())return *candidateAlias;
    return candidate;
}
// The row's table, looked up by the node's attr, then its key; a view with a single table is
// unambiguous. One lookup rule for the column reader and the segment reader, so the two cannot drift.
const QMap<QString,QString>* tableFor(const XmbView& host,const QString& attr,const QString& key,bool allowSingle) {
    auto table=host.tables.constFind(attr);
    if(table==host.tables.constEnd() && key!=attr)table=host.tables.constFind(key);
    if(table==host.tables.constEnd() && allowSingle && host.tables.size()==1)table=host.tables.constBegin();
    return table==host.tables.constEnd()?nullptr:&table.value();
}
// Every field the definition puts in a row's table. title/info/icon are what a column row needs; the
// remaining four are the settings pages' own semantics (child=segment, ch_pos, fo_lbl2_alpha,
// bar_action), carried verbatim so a caller never has to re-open the document to read them.
void readTableAttributes(XmbmlColumnRow& row,const QMap<QString,QString>& table) {
    row.hasTable=true;
    row.titleRsc=table.value(QStringLiteral("title_rsc"));
    row.iconRsc=table.value(QStringLiteral("icon_rsc"));
    row.infoRsc=table.value(QStringLiteral("info_rsc"));
    row.moduleName=table.value(QStringLiteral("module_name"));
    row.moduleAction=table.value(QStringLiteral("module_action"));
    row.child=table.value(QStringLiteral("child"));
    row.chPos=table.value(QStringLiteral("ch_pos"));
    row.foLbl2Alpha=table.value(QStringLiteral("fo_lbl2_alpha"));
    row.barAction=table.value(QStringLiteral("bar_action"));
}
XmbmlFallback fallbackFor(const XmbmlResolveOptions& options,const QString& itemKey,const QString& queryKey) {
    if(!itemKey.isEmpty()) {
        const auto found=options.fallbacks.constFind(itemKey);
        if(found!=options.fallbacks.cend() && !found->isEmpty())return *found;
    }
    const auto found=options.fallbacks.constFind(queryKey);
    if(found!=options.fallbacks.cend())return *found;
    return XmbmlFallback{};
}
// Resolve one row's title/info/icon through the container's text table and the caller's fallback,
// counting which source won. Shared by both readers: a label can only be resolved one way.
void resolveRowLabels(XmbmlResolvedColumn& column,XmbmlColumnRow& row,const XmbmlTextResolver& text,
                      const XmbmlResolveOptions& options) {
    const auto resolveText=[&](const QString& key) {
        if(key.isEmpty() || !text)return QString{};
        return text(column.container,key);
    };
    row.container=column.container;
    if(!row.titleRsc.isEmpty()) {
        row.title=resolveText(row.titleRsc);
        if(!row.title.isEmpty()){row.titleSource=FieldSource::Rco;++column.rcoRows;row.titleKey=row.titleRsc;}
    }
    if(!row.iconRsc.isEmpty()){row.icon=row.iconRsc;row.iconSource=FieldSource::Xmbml;}
    if(!row.infoRsc.isEmpty()) {
        row.detail=resolveText(row.infoRsc);
        if(!row.detail.isEmpty())row.detailSource=FieldSource::Rco;
    }
    const auto fallback=fallbackFor(options,row.itemKey,row.queryKey);
    // A label the firmware builds in a plugin: its resource key lives in the same container, so
    // the text is still the firmware's own and still goes through the locale table.
    if(row.title.isEmpty() && !fallback.titleRsc.isEmpty()) {
        row.title=resolveText(fallback.titleRsc);
        if(!row.title.isEmpty()){row.titleSource=FieldSource::Rco;++column.pluginRows;row.titleKey=fallback.titleRsc;}
    }
    if(row.icon.isEmpty() && !fallback.iconRsc.isEmpty()) {
        row.icon=fallback.iconRsc;row.iconSource=FieldSource::Fallback;
    }
    if(row.title.isEmpty() && !fallback.title.isEmpty()) {
        row.title=fallback.title;row.titleSource=FieldSource::Fallback;++column.fallbackRows;
    }
    if(row.icon.isEmpty() && !fallback.icon.isEmpty()) {
        row.icon=fallback.icon;row.iconSource=FieldSource::Fallback;
    }
    if(row.detail.isEmpty() && !fallback.detail.isEmpty()) {
        row.detail=fallback.detail;row.detailSource=FieldSource::Fallback;
    }
    if(row.title.isEmpty())++column.untitled;
}
}

QString queryKindName(QueryKind kind) {
    switch(kind) {
    case QueryKind::Local:return QStringLiteral("local");
    case QueryKind::Selector:return QStringLiteral("sel");
    case QueryKind::SystemQuery:return QStringLiteral("ese");
    case QueryKind::ContentBrowser:return QStringLiteral("xcb");
    case QueryKind::ContentList:return QStringLiteral("xil");
    case QueryKind::UserProvider:return QStringLiteral("user");
    case QueryKind::NowPlaying:return QStringLiteral("now");
    case QueryKind::Chat:return QStringLiteral("chat");
    case QueryKind::ExternalDocument:return QStringLiteral("xmb");
    case QueryKind::Unknown:break;
    }
    return QStringLiteral("unknown");
}
QString fieldSourceName(FieldSource source) {
    switch(source) {
    case FieldSource::Xmbml:return QStringLiteral("xmbml");
    case FieldSource::Rco:return QStringLiteral("rco");
    case FieldSource::Fallback:return QStringLiteral("fallback");
    case FieldSource::None:break;
    }
    return QStringLiteral("none");
}
RowKind XmbmlRow::kind() const {
    return rowKindFor(itemClass,provider,source,hasView);
}
RowKind XmbmlColumnRow::kind() const {
    if(inGameOnly)return RowKind::InGameExit;
    return rowKindFor(itemClass,provider,source,!dynamic);
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
QString categoryColumnPath(const QString& category,const XmbmlResolveOptions& options) {
    QString name=category.trimmed();
    if(name.isEmpty())name=QStringLiteral("category_network.xml");
    const bool separated=name.contains(QLatin1Char('/'))||name.contains(QLatin1Char('\\'));
    if(!separated && !name.endsWith(QLatin1String(".xml"),Qt::CaseInsensitive))
        name=name.startsWith(QLatin1String("category_"))?name+QStringLiteral(".xml")
                                                       :QStringLiteral("category_")+name+QStringLiteral(".xml");
    if(QDir::isAbsolutePath(name))return QDir::cleanPath(name);
    const QDir root(options.flashRoot);
    if(separated)return options.flashRoot.isEmpty()?QDir::cleanPath(name)
                                                  :QDir::cleanPath(root.filePath(name));
    if(options.flashRoot.isEmpty())return name;
    return QDir::cleanPath(root.filePath(QStringLiteral("vsh/resource/explore/xmb/")+name));
}
XmbmlResolvedColumn resolveCategoryColumn(const QString& category,const XmbmlTextResolver& text,
                                          const XmbmlResolveOptions& options) {
    XmbmlResolvedColumn column;
    column.viewId=options.viewId.isEmpty()?QStringLiteral("root"):options.viewId;
    column.path=categoryColumnPath(category,options);
    column.category=categoryStem(column.path);
    column.container=options.container.isEmpty()?QStringLiteral("explore_category_")+column.category
                                                :options.container;
    int rowLimit=options.rowLimit;
    if(rowLimit<=0 || rowLimit>128)rowLimit=64;
    if(!QFileInfo(column.path).isFile()) {
        column.error=QStringLiteral("XMBML definition not found: ")+column.path;
        return column;
    }
    const auto document=readXmbDocument(column.path);
    column.sha256=document.sha256;
    if(!document.error.isEmpty()){column.error=document.error;return column;}
    const auto root=document.views.constFind(column.viewId);
    if(root==document.views.constEnd()) {
        column.error=QStringLiteral("view \"")+column.viewId+QStringLiteral("\" is not defined");
        return column;
    }
    QSet<QString> seenIds;
    // Resolve one row's table fields, text and id, then append it. `tableHost` is the view that
    // carries the attribute table: the expanded view for a row, the root view for a provider query.
    const auto append=[&](XmbmlColumnRow row,const XmbView& tableHost,const QString& lookupAttr,
                          const QString& lookupKey,bool allowsSingleTable) {
        if(column.rows.size()>=rowLimit){++column.skipped;return;}
        if(const auto* table=tableFor(tableHost,lookupAttr,lookupKey,allowsSingleTable))readTableAttributes(row,*table);
        resolveRowLabels(column,row,text,options);
        row.order=column.rows.size();
        if(seenIds.contains(row.id))++column.duplicateIds;
        else seenIds.insert(row.id);
        column.rows.push_back(std::move(row));
    };
    for(const auto& node:root->items) {
        if(node.kind!=QLatin1String("Query")){++column.skipped;continue;}
        const auto queryKey=node.attributes.value(QStringLiteral("key"));
        if(queryKey.isEmpty()){++column.skipped;continue;}
        ++column.queries;
        XmbmlColumnRow top;
        top.queryKey=queryKey;
        top.source=node.attributes.value(QStringLiteral("src"));
        top.provider=providerFromSource(top.source);
        top.viewId=viewFromSource(top.source);
        top.itemClass=node.attributes.value(QStringLiteral("class"));
        top.inGameOnly=top.source.startsWith(QLatin1String("sel://localhost/ingame"));
        top.query=queryKindForProvider(top.provider);
        const auto view=document.views.constFind(top.viewId);
        const bool hasView=view!=document.views.constEnd() && !top.viewId.isEmpty();
        if(hasView)top.query=QueryKind::Local;
        if(!hasView || view->items.isEmpty()) {
            // A Query with no local view, or a view that only carries tables: one row produced at
            // run time by the provider (xil/xcb/user/...), titled from the root view's table if the
            // definition gives it one, else from the caller's fallback.
            XmbmlColumnRow row=top;
            row.dynamic=!hasView;
            row.itemClass=hasView?QString{}:top.itemClass;
            row.id=nativeId(queryKey,options.idAliases);
            const auto attr=node.attributes.value(QStringLiteral("attr"));
            append(std::move(row),*root,attr.isEmpty()?queryKey:attr,queryKey,true);
            continue;
        }
        // The view's <Items> are the rows the column shows, in document order: <Item> nodes are
        // leaves and nested <Query> nodes are child folders (the Settings column writes Game
        // Settings, Accessory Settings and Network Settings that way).
        int rowNodes=0;
        for(const auto& child:view->items)if(child.kind==QLatin1String("Item")||child.kind==QLatin1String("Query"))++rowNodes;
        if(rowNodes>1)++column.expanded;
        if(rowNodes==0) {
            XmbmlColumnRow row=top;
            row.id=nativeId(queryKey,options.idAliases);
            const auto attr=node.attributes.value(QStringLiteral("attr"));
            append(std::move(row),*view,attr.isEmpty()?queryKey:attr,queryKey,true);
            continue;
        }
        for(const auto& child:view->items) {
            if(child.kind!=QLatin1String("Item") && child.kind!=QLatin1String("Query")){++column.skipped;continue;}
            XmbmlColumnRow row=top;
            row.itemKey=child.attributes.value(QStringLiteral("key"));
            row.itemClass=child.attributes.value(QStringLiteral("class"));
            row.folder=child.kind==QLatin1String("Query") ||
                       row.itemClass==QLatin1String("type:x-xmb/folder-pixmap");
            row.nested=child.kind==QLatin1String("Query");
            // A child Query names its own target view; the row's label still lives in the table of
            // the view it sits in (seg_fixed_items: table "device" -> msg_pd_setting).
            const auto childSource=child.attributes.value(QStringLiteral("src"));
            if(row.nested)row.source=childSource.isEmpty()?row.source:childSource;
            row.id=nativeId(row.itemKey.isEmpty()?queryKey:row.itemKey,options.idAliases);
            const auto attr=child.attributes.value(QStringLiteral("attr"));
            append(std::move(row),*view,attr.isEmpty()?row.itemKey:attr,row.itemKey,rowNodes==1);
        }
    }
    if(column.rows.isEmpty())
        column.error=QStringLiteral("the \"")+column.viewId+QStringLiteral("\" view defines no rows");
    return column;
}
XmbmlResolvedColumn resolveSegmentView(const QString& category,const QString& viewId,
                                       const XmbmlTextResolver& text,
                                       const XmbmlResolveOptions& options) {
    XmbmlResolvedColumn column;
    column.viewId=viewId;
    column.path=categoryColumnPath(category,options);
    column.category=categoryStem(column.path);
    column.container=options.container.isEmpty()?QStringLiteral("explore_category_")+column.category
                                                :options.container;
    int rowLimit=options.rowLimit;
    if(rowLimit<=0 || rowLimit>128)rowLimit=64;
    if(!QFileInfo(column.path).isFile()) {
        column.error=QStringLiteral("XMBML definition not found: ")+column.path;
        return column;
    }
    const auto document=readXmbDocument(column.path);
    column.sha256=document.sha256;
    if(!document.error.isEmpty()){column.error=document.error;return column;}
    const auto segment=document.views.constFind(viewId);
    if(segment==document.views.constEnd()) {
        column.error=QStringLiteral("view \"")+viewId+QStringLiteral("\" is not defined");
        return column;
    }
    QSet<QString> seenIds;
    // One pass over the segment view's own item order; each child Query names the group view whose
    // <Items> are the page's rows (the settings the group binds, in that group's order).
    for(const auto& node:segment->items) {
        if(node.kind!=QLatin1String("Query")){++column.skipped;continue;}
        const auto groupKey=node.attributes.value(QStringLiteral("key"));
        if(groupKey.isEmpty()){++column.skipped;continue;}
        const auto groupSource=node.attributes.value(QStringLiteral("src"));
        const auto groupView=viewFromSource(groupSource);
        ++column.queries;
        const auto group=document.views.constFind(groupView);
        if(group==document.views.constEnd()){++column.skipped;continue;}
        int rowNodes=0;
        for(const auto& child:group->items)
            if(child.kind==QLatin1String("Item")||child.kind==QLatin1String("Query"))++rowNodes;
        if(rowNodes==0){++column.skipped;continue;}
        for(const auto& child:group->items) {
            if(child.kind!=QLatin1String("Item") && child.kind!=QLatin1String("Query")){++column.skipped;continue;}
            if(column.rows.size()>=rowLimit){++column.skipped;continue;}
            XmbmlColumnRow row;
            row.queryKey=groupKey;
            row.itemKey=child.attributes.value(QStringLiteral("key"));
            row.itemClass=child.attributes.value(QStringLiteral("class"));
            row.viewId=groupView;
            row.source=groupSource;
            row.provider=providerFromSource(groupSource);
            row.query=QueryKind::Local;
            row.folder=child.kind==QLatin1String("Query");
            row.id=nativeId(row.itemKey.isEmpty()?groupKey:row.itemKey,options.idAliases);
            const auto attr=child.attributes.value(QStringLiteral("attr"));
            if(const auto* table=tableFor(*group,attr.isEmpty()?row.itemKey:attr,row.itemKey,rowNodes==1))
                readTableAttributes(row,*table);
            resolveRowLabels(column,row,text,options);
            row.order=column.rows.size();
            if(seenIds.contains(row.id))++column.duplicateIds;
            else seenIds.insert(row.id);
            column.rows.push_back(std::move(row));
        }
    }
    if(column.rows.isEmpty())
        column.error=QStringLiteral("the \"")+viewId+QStringLiteral("\" view defines no rows");
    return column;
}
} // namespace xmb
