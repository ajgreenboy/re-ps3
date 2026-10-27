#pragma once
#include <QString>
#include <QVector>

namespace xmb {

// One row of a category's root XMBML view. The firmware describes a column as a list of <Query>
// nodes, each pointing at a "pixmap view" whose <Table> carries the row's title_rsc and icon_rsc.
// This struct is what that definition says, before any native action is chosen: the strings are
// resource keys, not text, and the class is the firmware's own address of the item type.
enum class RowKind {
    Unknown,         // a class this build does not know
    WebBrowser,      // type:x-xmb/xmlwebbrowser - handed to the PS3 Internet Browser
    ModuleAction,    // type:x-xmb/module-action - a plugin (module_name:module_action) does the work
    RemotePlay,      // type:x-xmb/xmlpremo - Remote Play, needs console hardware
    InGameExit,      // type:x-xmb/xmlgameexit - only the in-game XMB shows it
    DynamicProvider, // a Query with no local view: sel/ese/xcb produces its rows at run time
};
struct XmbmlRow {
    QString id;        // stable native id: the query key without the firmware's "seg_" namespace
    QString key;       // the <Query key="..."> value, verbatim ("seg_browser")
    QString viewId;    // the pixmap view the src fragment names (empty for a provider query)
    QString itemClass; // the <Item class="..."> inside that view ("type:x-xmb/xmlwebbrowser")
    QString titleRsc;  // Table title_rsc ("msg_browser")
    QString iconRsc;   // Table icon_rsc ("tex_browser")
    QString infoRsc;   // Table info_rsc, when the view carries one
    QString moduleName;   // Table module_name, for a module-action row ("kensaku_plugin")
    QString moduleAction; // Table module_action, for a module-action row ("kensaku_google")
    QString source;    // the Query src attribute, verbatim
    QString provider;  // src scheme before "://" ("sel", "ese", "xcb", "xmb"), empty for "#view"
    QString itemKey;   // the <Item key="..."> inside the view
    bool hasView=false;  // the src names a view this document defines
    bool hasTable=false; // that view carries the attribute table with the row's resources
    bool inGameOnly=false; // the src is the in-game selector (sel://localhost/ingame)
    RowKind kind() const;
};

struct XmbmlColumn {
    QString path,sha256,viewId;
    QVector<XmbmlRow> rows;
    int queries=0;  // <Query> nodes seen in the root view (rows stops at the row limit)
    QString error;  // non-empty: the caller must keep its built-in rows
    bool ok() const { return error.isEmpty(); }
};

// Read one category's root view from the local dev_flash XMBML and resolve every Query to the
// pixmap view it names. Read-only (the firmware tree is never written), bounded (file size and node
// count inside readXmbDocument, at most rowLimit rows here), and never throws: a missing file, a
// malformed document (readXmbDocument reports DTDs, duplicate view ids, node overflow and XML
// errors) or a root view with no rows comes back as XmbmlColumn::error.
XmbmlColumn readCategoryView(const QString& filePath,const QString& viewId=QStringLiteral("root"),int rowLimit=32);

} // namespace xmb
