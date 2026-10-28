#pragma once
#include "shell.h"

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

} // namespace xmb
