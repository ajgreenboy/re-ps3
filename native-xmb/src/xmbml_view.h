#pragma once
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

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
    DownloadControl, // ese://...type=download_control - the download queue the console's plugin lists
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

// --- Generalised column resolution (Phase 3.1) ------------------------------------------------
//
// The 4.93 firmware defines every column as explore/xmb/category_<name>.xml. A column is the root
// view's ordered <Query> list. A Query names a pixmap view (in the same file, in another XMBML file
// through xmb://, or through a plugin scheme: sel/ese/xcb/xil/user/now/chat). That view's <Items>
// are the rows the column shows: <Item> nodes are leaf rows, and nested <Query> nodes are child
// folders (the Settings column's Game Settings / Accessory Settings / Network Settings rows are
// written that way), interleaved in document order. A Query with no local view (xil://, xcb://,
// user://, ...) produces its rows at run time: it is one row here, and the caller decides what to
// put in it.
//
// Text resolution is the caller's RCO lookup (Assets::text in the shell): title_rsc/info_rsc are
// resource keys resolved through the category's own container (explore_category_<name>.rco) and its
// English table. Icons are texture names and need no lookup. The reader never invents a label: a row
// the definition leaves untitled is marked unresolved, and the caller supplies a per-column fallback
// (options.fallbacks) or logs the row away. That is the "per-column fallback result": XmbmlResolvedColumn
// carries the counters and the reader returns rows in document order so the caller can fall back
// row-by-row, and the whole column when error is set.

// The scheme of a Query's src, i.e. which firmware plugin produces the rows.
enum class QueryKind {
    Local,            // "#view", or a src whose fragment names a view in this document
    Selector,         // sel://  the vsh selector plugin (a local view selected by a condition)
    SystemQuery,      // ese://  system queries: install lists, the download queue
    ContentBrowser,   // xcb://  the media content browser (dlna/data devices, HDD contents)
    ContentList,      // xil://  the item-list plugin (gameDir / music / video / tv)
    UserProvider,     // user://  category_user's own user list and user creation
    NowPlaying,       // now://   the music column's now-playing provider
    Chat,             // chat://  friend/chat room list
    ExternalDocument, // xmb://   another XMBML file's view
    Unknown,
};
QString queryKindName(QueryKind kind);

// Where a resolved field came from, so a caller can log it and an audit can count it.
enum class FieldSource {
    None,     // the definition (and the fallback) had nothing
    Xmbml,    // icon_rsc/title_rsc named it (an icon name needs no lookup)
    Rco,      // a title_rsc/info_rsc key resolved through the RCO text tables
    Fallback, // the caller's last-resort label (options.fallbacks)
};
QString fieldSourceName(FieldSource source);

// A last-resort label for a row the firmware's own definition leaves untitled. Keyed by the
// firmware item key (or the query key), because that is the definition's own identity for the row.
// titleRsc/iconRsc exist because the firmware builds some labels in a plugin rather than in the
// view's <Table> (the Network column's Remote Play and Download Management rows are bare items, and
// their strings live in the same RCO under msg_psp_connection / msg_download_control): a resource
// key here is resolved through the same container and locale as the definition's own keys, so the
// text is still the firmware's and still localized. Literal title/icon/detail are the final resort.
struct XmbmlFallback {
    QString title, icon, detail;
    QString titleRsc, iconRsc;
    bool isEmpty() const {
        return title.isEmpty() && icon.isEmpty() && detail.isEmpty() && titleRsc.isEmpty() && iconRsc.isEmpty();
    }
};

struct XmbmlColumnRow {
    int order=0;            // display position in the column, from document order
    QString id;             // native id: the idAliases entry, else the item key, else the query key minus "seg_"
    QString queryKey;       // the top-level <Query key="..."> the row came from ("seg_fixed_items")
    QString itemKey;        // the <Item>/<Query> key inside that view ("update"), empty for a provider row
    QString itemClass;      // that node's class ("type:x-xmb/xmlsysconf")
    QString viewId;         // the view the src fragment names
    QString source;         // the top-level Query src verbatim
    QString provider;       // src scheme, lower case, empty for "#view"
    QueryKind query=QueryKind::Unknown;
    QString container;      // the RCO container the strings were resolved from
    QString titleRsc,iconRsc,infoRsc;
    QString titleKey;       // the resource key the title actually came from (the definition's, or a fallback's)
    QString title,icon,detail;
    FieldSource titleSource=FieldSource::None,iconSource=FieldSource::None,detailSource=FieldSource::None;
    QString moduleName,moduleAction;
    // The row's own <Table> attributes the settings pages are defined by (audit/SETTINGS-FIRMWARE.md
    // 1.1): the firmware writes the three two-level folder rows as child=segment / ch_pos=1, and it
    // suppresses their second label line with fo_lbl2_alpha=0. These are the strings verbatim, empty
    // when the row's table has no such pair, so the caller decides what they mean.
    QString child;        // "segment" on Game/Accessory/Network Settings
    QString chPos;        // "1" on those three rows
    QString foLbl2Alpha;  // "0": this row has no second label line (no native info line)
    QString barAction;    // "none": the XMB bar itself does nothing on Cross, a plugin takes over
    bool hasTable=false;
    bool segment() const { return child==QLatin1String("segment"); }
    // The definition's second label line is drawn unless its alpha is exactly 0 (fo_lbl2_alpha=0);
    // a row whose table says nothing keeps today's behaviour.
    bool hasSecondLabel() const { return foLbl2Alpha!=QLatin1String("0"); }
    bool inGameOnly=false;  // sel://localhost/ingame: only the in-game XMB shows the row
    bool folder=false;      // the row opens another list (a child <Query>, or a folder class)
    bool dynamic=false;     // no local view: the provider produces the row's content at run time
    bool nested=false;      // the row is a child <Query> inside the expanded folder view
    RowKind kind() const;
};

struct XmbmlResolveOptions {
    QString flashRoot;       // dev_flash root; a bare category name resolves under vsh/resource/explore/xmb
    QString viewId=QStringLiteral("root");
    QString container;       // override; default explore_category_<name> (the column's own RCO)
    QHash<QString,QString> idAliases;      // firmware key -> native id this build already uses
    QHash<QString,XmbmlFallback> fallbacks; // firmware key -> last-resort label
    int rowLimit=64;
};

struct XmbmlResolvedColumn {
    QString path,sha256,category,container,viewId;
    QVector<XmbmlColumnRow> rows;
    int queries=0;        // top-level <Query> nodes in the root view
    int expanded=0;       // queries whose view carried more than one row
    int skipped=0;        // queries without a key, or nodes that are not rows
    int untitled=0;       // rows with no title after the fallback pass
    int fallbackRows=0;   // rows whose title or icon came from options.fallbacks
    int rcoRows=0;        // rows whose title resolved through the RCO text table
    int pluginRows=0;     // rows titled by a fallback titleRsc (a plugin-built label, still RCO text)
    int duplicateIds=0;   // rows whose native id repeats an earlier row's (a caller alias mistake)
    QString error;        // non-empty: the caller must keep the column's built-in rows
    bool ok() const { return error.isEmpty(); }
};

// The RCO text lookup: (container, resource key) -> localized string, empty when absent.
using XmbmlTextResolver = std::function<QString(const QString& container,const QString& key)>;

// Resolve a column. `category` is "category_sysconf.xml", "sysconf", or a path; with flashRoot set a
// bare name resolves under <flashRoot>/vsh/resource/explore/xmb/. Read-only and bounded exactly like
// readCategoryView; a document that cannot be read, has no root view, or produces no row at all comes
// back with error set, which is the per-column fallback the caller keeps its built-in rows for.
XmbmlResolvedColumn resolveCategoryColumn(const QString& category,const XmbmlTextResolver& text,
                                         const XmbmlResolveOptions& options=XmbmlResolveOptions{});

// Resolve a *segment* view: Settings' seg_game, seg_device and seg_network are what the column's three
// child=segment rows open, and their <Items> are child Queries naming the group views the page lists
// (seg_game -> seg_game_fixed_upper|middle|lower, seg_device -> its six groups, seg_network ->
// seg_network_fixed and seg_network_dlnaconf). The column reader deliberately stops at the folder row;
// this is the one level below it: each group view's <Items> become rows, in the segment view's own item
// order and then each group's item order, with the same table lookup, text resolution, fallbacks and id
// aliasing the column reader uses. `queryKey` names the group ("seg_game_fixed_upper"), `viewId` is the
// group view, and `segment()`/`hasSecondLabel()` carry that group's own table attributes.
// A group whose view is absent or empty is counted in `skipped`, never invented.
XmbmlResolvedColumn resolveSegmentView(const QString& category,const QString& viewId,
                                       const XmbmlTextResolver& text,
                                       const XmbmlResolveOptions& options=XmbmlResolveOptions{});

// The absolute path resolveCategoryColumn used for `category` (for the caller's log line).
QString categoryColumnPath(const QString& category,const XmbmlResolveOptions& options=XmbmlResolveOptions{});

} // namespace xmb
