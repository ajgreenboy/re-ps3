#pragma once
#include <QtGlobal>

namespace xmb {
// PlayStation controllers read straight over HID: DualShock 3 (USB), DualShock 4 and DualSense (USB or Bluetooth),
// so no driver or Steam layer is needed. Ported from MaXMB (MIT, src-tauri/src/hidpad.rs): the same report layouts
// and start-up feature reports. A watcher thread looks for controllers every 2 s and gives each one a reader thread;
// the newest reading is kept here and goes stale after 300 ms.
namespace pad {
// Buttons in XInput's wButtons layout, so callers handle both kinds of controller alike:
// D-pad 0x1/0x2/0x4/0x8, START 0x10, SELECT 0x20, L3 0x40, R3 0x80, L1 0x100, R1 0x200, PS 0x400,
// ✕ 0x1000, ○ 0x2000, □ 0x4000, △ 0x8000.
struct State { quint16 buttons=0; float lx=0,ly=0,rx=0,ry=0,l2=0,r2=0; };
// Starts the watcher once (Windows only; elsewhere a no-op).
void start();
// The newest reading from any PlayStation controller, if one reported in the last 300 ms.
bool latest(State& out);
}
}
