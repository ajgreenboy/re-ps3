#pragma once
#include <QString>

// D9: the per-app display request an apps.json entry may carry: "display": { "width", "height", "refresh", "hdr" }.
// Plain data so content_registry (also built into the manager) needs no display code. Zero means "keep the current
// value"; hdr is "on", "off" or "keep" (empty = keep). width and height are given together or not at all.
namespace xmb {
struct DisplaySpec {
    bool present=false;
    int width=0,height=0,refresh=0;
    QString hdr;
    bool operator==(const DisplaySpec& o) const {
        return present==o.present && width==o.width && height==o.height && refresh==o.refresh && hdr==o.hdr;
    }
};
}
