#pragma once
#include "shell.h"
#include <functional>

namespace xmb {

// The two file pickers that live inside the Theme and Background choice lists rather than on the Theme page.
// The console's Theme page is Theme / Color / Background / Font and stops there (SETTINGS-FIRMWARE 5.6), and the
// lists are the two places the firmware folds theme and wallpaper selection into (D11), so `Install` (the Theme
// list's own first item) and `Select Wallpaper` (the Background list's last) carry these sentinel values. No
// bounded Preferences key accepts them: Shell::activate() turns a SetSetting row with one of these values into
// the .p3t / image file dialog before the write chain runs, so nothing is ever stored.
inline constexpr const char* kThemePickerValue = "install-theme-file";
inline constexpr const char* kWallpaperPickerValue = "select-wallpaper-file";
// The Settings column's rows, resolved from the firmware's own definition (Phase 11.1,
// audit/SETTINGS-FIRMWARE.md 1.1-1.2). `category_sysconf.xml`'s root view names `#seg_fixed_items`,
// whose 18 tables list the column's 18 rows in document order. The 16 rows a retail console shows are
// kept in that order, with the RCO's own title, info line and icon (the `tex_*` icons of
// explore_category_sysconf, through Assets::text and the shared image path); `edy` and `console_qa`
// are omitted because the console gates them (the Japan-only Edy Viewer and the QA Debug Settings) and
// the reason is logged. The three child rows (Game/Accessory/Network Settings) carry
// child=segment / ch_pos=1 / fo_lbl2_alpha=0, and the console still draws their info line (the frames
// col-settings-r01/r09/r15 show it), so the native row keeps it too: C-1 in SETTINGS-COVERAGE 1.1.
//
// `builtin` is this build's hand-written Settings column. The resolver keeps each id's native action,
// value and setting (which page it opens, and the segment pages for the three folders) and replaces
// only title, detail, icon and order, exactly as the other XMBML columns do. An empty result means the
// definition could not be read: the caller keeps its built-in rows (the same per-column fallback the
// Network column uses) and the reason has been logged.
QVector<Entry> settingsColumnEntries(const QVector<Entry>& builtin,Assets& assets);

// --- Phase 11.3: the choice list a settings row opens ---------------------------------------------
//
// The firmware's own choice pages are page class B: `Page { Plane { BG0, BG1, LowerMenu(MList) ->
// MItem... } }` (audit/SETTINGS-FIRMWARE.md 3.1). Each MItem carries its own label as a text reference
// (`text = text#N:msg_*`), so the list and its order are the RCO's, not this build's: enumerating the
// page's LowerMenu children in document order reproduces the labels the capture batch measured
// (`After 5 Minutes` ... `Do Not Use`; `Install`, `Original`, `Classic`, `Air Paint`). The current-value
// mark is NOT in the RCO (the MItems have no children, SETTINGS 3.1/U4), so it is computed here from the
// persisted preference; the frames show the current item focused with no check or dot.
//
// `value` is what the existing `Action::SetSetting` path stores: the same value `choices()` returns for
// the same semantic item, so a view pass can draw this list and still call `Shell::openChoices`'
// semantics unchanged (Phase 11.1 left `choices()` byte-identical; this is the list that replaces it
// when the band renderer lands). `writes` is false for a firmware item this build cannot honour yet: its
// `value` is then a sentinel outside every bounded key (an RCO list uses the item's ordinal; the Theme
// page's run-time lists use 1000 + the authored index, because their real values are small integers), not
// a preference value, and a view must not commit it.
struct ChoiceItem {
    QString label;          // the item's own text, from the RCO's English table
    QVariant value;         // what SetSetting stores (same as choices()); sentinel when !writes
    bool isCurrent=false;   // matches the persisted preference (never from the RCO: SETTINGS U4)
    QString source;         // "rco" | "native" | "capture"
    bool scrollable=false;  // the list is longer than the measured band, or the page carries a Scroll
    QString item;           // the RCO MItem object name (provenance), empty when not from an RCO
    int rcoIndex=-1;        // the item's position in the page's LowerMenu
    quint32 rcoOffset=0;    // its offset in sysconf_plugin.rco's table section
    bool writes=false;      // `value` is a real preference value for this build
    // Phase 11.4 (audit/SETTINGS-FIRMWARE.md 16): the Theme page's run-time lists are not text rows.
    // `swatch` says the row is a colour chip and `colour` is the 0xRRGGBB it is drawn with (measured on the
    // capture, which is the chip's only identity - the console gives it no label); `submenu` is the
    // firmware's right-hand chevron row (`Brightness`); `note` is the item's own reason when !writes, so a
    // disabled item can say why instead of pretending. All four are empty/zero for every RCO list.
    bool swatch=false;
    quint32 colour=0;
    bool submenu=false;
    QString note;
};
struct ChoiceList {
    QString pageId;         // the settings page ("display", "date", ...)
    QString rowId;          // the row on that page ("screensaver", "timezone", ...)
    QString rowTitle;       // the row's label, for the dump
    QString setting;        // the Preferences key the row writes (empty for an inert row)
    QString rcoPage;        // the sysconf_plugin page object (empty: no firmware choice page)
    quint32 rcoPageOffset=0;
    QString container;      // "sysconf_plugin" when the list came from the RCO
    QString source;         // "rco" | "rco-page-empty" | "capture" | "native" | "none"
    QString note;           // why a fallback, or which attestation the list has
    bool scrollable=false;
    QString scrollObject;   // the page's Scroll object when the RCO declares one
    // The number of rows the captured band shows at once for this list (0: the band's own 27-row capacity,
    // SETTINGS 11.4). The Color list measures 12 visible rows at the 40 px pitch, then a down-chevron; a
    // longer list scrolls one row per focus step past the window (SETTINGS 16.2).
    int windowRows=0;
    QVector<ChoiceItem> items;
    // The item the persisted value selects: its rcoIndex (the row the band focuses), or -1 when nothing
    // in the list is current. A time zone stored as an IANA id has no firmware item, so this is -1 and the
    // band is told not to guess a row (SETTINGS 13.6a/14); a firmware zone token reports its item.
    int currentRow=-1;
    bool fromRco() const { return source==QLatin1String("rco"); }
    int count() const { return int(items.size()); }
};

// The firmware's own list for one settings row, ordered as the page authors it. `rowId` is the row's
// native id on `pageId`, or its Preferences key; `current` is the value the row currently holds (the
// caller reads it, because a free function has no Shell). An empty result means the row is not a choice
// row, or its page could not be read (the reason is on the ChoiceList and in the log).
QVector<ChoiceItem> settingsChoices(const QString& pageId,const QString& rowId,Assets& assets,
                                    const QVariant& current=QVariant());
// Every choice row of one page, with provenance. `currentFor` maps a Preferences key to its current
// value (the caller's Shell::preferences_); an empty function marks no item current.
QVector<ChoiceList> settingsChoiceLists(const QString& pageId,Assets& assets,
    const std::function<QVariant(const QString&)>& currentFor={});
// Every choice row of every settings page, in Settings-column order, one record per list. This is the
// aggregate `<XMB_DUMP_ENTRIES>.settings-choices.json` catalogue: it reads sysconf_plugin.rco once.
QVector<ChoiceList> settingsChoiceCatalogue(Assets& assets,
    const std::function<QVariant(const QString&)>& currentFor={});
// True when `current` is the item's value. QVariant compares int/bool/string strictly, and the store may
// hold an int where a choice is a bool (or the reverse), so this also compares the numeric and text forms.
bool choiceValueMatches(const QVariant& current,const QVariant& value);

// The label an app-owned choice row shows for a stored value: the same choices() table (and the same
// firmware strings) the value band commits, so a row's value and its band's focused item are one string.
// The host-driven keys read their value from the machine (host::snapshot()) rather than the store.
QString settingsChoiceLabel(const QString& key,const QVariant& value,Assets& assets);

// The body of one settings row's information page. The host layer's own page (host::rowDetail) when it has
// one; System Information's host report; otherwise this build's page for the row, composed from the row's
// title, the firmware's explanation (Entry::detail), the host fact that leaves its value column empty
// (host::rowValue's note) and - where the RCO carries the row's own option page - the console's option
// labels read from that page's MItems. Never empty for a settings row, so a settings row can never fall
// through to another row's body. `renderer` and `systemName` are the two values only the caller knows; the
// report passes none and the shell passes its own.
QString settingsRowBody(const Entry& row,Assets& assets,const QString& renderer={},const QString& systemName={});

// The whole Settings surface as one JSON document: every page, every row, the live value the row shows,
// where that value came from ("win32", "qt", "com", "hid", "app", ...), what a Cross on the row does
// ("action", "read-only", "live"), and the machine readings behind them. `--settings-report` prints it, so
// the coverage is verifiable without screenshots.
QJsonObject settingsReport(Assets& assets,Preferences& preferences,const QString& dataDirectory);

} // namespace xmb
