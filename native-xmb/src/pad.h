#pragma once
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace xmb {
// PlayStation controllers read straight over HID: DualShock 3 (USB), DualShock 4 and DualSense (USB or Bluetooth),
// so no driver or Steam layer is needed. Ported from MaXMB (MIT, src-tauri/src/hidpad.rs): the same report layouts
// and start-up feature reports. A watcher thread looks for controllers every 2 s and gives each one a reader thread;
// the newest reading is kept here and goes stale after 300 ms.
namespace pad {
// Buttons in XInput's wButtons layout, so callers handle both kinds of controller alike:
// D-pad 0x1/0x2/0x4/0x8, START 0x10, SELECT 0x20, L3 0x40, R3 0x80, L1 0x100, R1 0x200, PS 0x400,
// Cross 0x1000, Circle 0x2000, Square 0x4000, Triangle 0x8000.
struct State { quint16 buttons=0; float lx=0,ly=0,rx=0,ry=0,l2=0,r2=0; };
// What is attached, how, and its battery as the report carries it (Phase 4.3, audit/SESSION-POWER.md).
enum class Kind { None, Ds3, Ds4, DualSense };
// The input report's own id names the transport, so it is read from the report rather than guessed from a device
// path: a DualSense sends 0x01 over USB and 0x31 over Bluetooth, a DualShock 4 sends 0x01 and 0x11. A DualShock 3
// in this build is USB only.
enum class Transport { Unknown, Usb, Bluetooth };
// batteryPercent is -1 when the report does not carry a level (DualShock 3), when the report is too short to hold
// the battery byte, or when the level field says "unknown" (a DualSense voltage/temperature/charging error).
// charging is "discharging", "charging", "full", "not-charging" or "unknown"; empty when there is no level.
struct Info { Kind kind=Kind::None; Transport transport=Transport::Unknown; int batteryPercent=-1; QString charging; };
// Starts the watcher once (Windows only; elsewhere a no-op).
void start();
// The newest reading from any PlayStation controller, if one reported in the last 300 ms.
bool latest(State& out);
// ---- Scripted-run input gate (audit/PS-BUTTON.md) ----
// A scripted capture must be deterministic: the script drives and a live controller must not. A run is scripted
// when --script is present or XMB_INPUT_SCRIPT_ONLY=1; main.cpp calls setScriptOnly() once, before the View (and
// so before pad::start()) exists. While the gate is on, latest() reports NO live state at all: a D-pad
// direction, stick axis, trigger, button or PS bit from a physical controller is still parsed, observed and
// counted, but never delivered, so View::pollController() cannot merge it into navigation and PsWatcher cannot
// open the system menu with it. The controller itself keeps being read (info() still answers --pad-report and
// the battery line still logs); only its influence on the shell is removed. Off by default: with no --script and
// no XMB_INPUT_SCRIPT_ONLY, latest() behaves exactly as it did before this gate existed.
void setScriptOnly(bool on);
bool scriptOnly();
// Live pad samples withheld because the run is scripted: one per latest() call that saw a live state carrying a
// button bit, a trigger past 0.5 or a stick axis past the same 0.5 deadzone pollController uses. A scripted
// sidecar publishes it as padEventsIgnored, so a capture proves the pad was not steering it. Always 0 in a
// normal run. XMB_INPUT_TRACE=1 logs each decision this count is taken from, source=pad.
quint64 ignoredEvents();
// The identity, transport and battery of the controller behind latest(), on the same 300 ms freshness window.
// False when no controller reported recently; out is left untouched then.
bool info(Info& out);
// One input report through the same parser the watcher uses (report id in byte 0, as Windows returns it). The
// battery byte is read only when the report is long enough for the layout's own offset, so a short or unknown
// report is rejected instead of reading past its end. Local verification runs documented synthetic reports here.
bool parseReport(Kind kind,const QVector<quint8>& report,Info& out);
QString kindName(Kind kind);          // "DualShock 3", "DualShock 4", "DualSense", "None"
QString transportName(Transport kind); // "USB", "Bluetooth", "Unknown"
}
}
