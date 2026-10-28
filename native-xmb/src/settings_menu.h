#pragma once
#include "shell.h"
#include <functional>

namespace xmb {

// The Settings column's rows, resolved from the firmware's own definition (Phase 11.1,
// audit/SETTINGS-FIRMWARE.md 1.1-1.2). `category_sysconf.xml`'s root view names `#seg_fixed_items`,
// whose 18 tables list the column's 18 rows in document order. The 16 rows a retail console shows are
// kept in that order, with the RCO's own title, info line and icon (the `tex_*` icons of
// explore_category_sysconf, through Assets::text and the shared image path); `edy` and `console_qa`
// are omitted because the console gates them (the Japan-only Edy Viewer and the QA Debug Settings) and
// the reason is logged. The three child rows (Game/Accessory/Network Settings) carry
// child=segment / ch_pos=1 / fo_lbl2_alpha=0: they are folders, so their native row keeps no info line.
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
// `value` is then the item's ordinal in the page, not a preference value, and a view must not commit it.
struct ChoiceItem {
    QString label;          // the item's own text, from the RCO's English table
    QVariant value;         // what SetSetting stores (same as choices()); ordinal when !writes
    bool isCurrent=false;   // matches the persisted preference (never from the RCO: SETTINGS U4)
    QString source;         // "rco" | "native" | "capture"
    bool scrollable=false;  // the list is longer than the measured band, or the page carries a Scroll
    QString item;           // the RCO MItem object name (provenance), empty when not from an RCO
    int rcoIndex=-1;        // the item's position in the page's LowerMenu
    quint32 rcoOffset=0;    // its offset in sysconf_plugin.rco's table section
    bool writes=false;      // `value` is a real preference value for this build
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

} // namespace xmb
