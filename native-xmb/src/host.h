#pragma once
#include <QString>
#include <functional>

namespace xmb::host {
// The machine the XMB runs on, for the settings that map onto it (System Information, Network status, Video
// Output, Volume). Everything here reads or writes the real host; nothing is simulated.
using Label=std::function<QString(const QString& id,const QString& fallback)>; // firmware string lookup
QString systemInformation(const QString& renderer,const Label& label,const QString& systemName=QString()); // PS3 fields, then CPU, memory, GPU, displays
QString networkStatus(const Label& label);             // physical adapters that are up, in the PS3 list's fields
QString audioOutput();                                 // default playback device name and its volume
QString displayOutput();                              // current resolution and refresh rate of each screen
// Windows default playback device (Core Audio endpoint). Volume 0-100; -1 / false when unavailable.
int masterVolume();
bool setMasterVolume(int percent);
bool masterMuted();
bool setMasterMuted(bool muted);
// Holds one system-wide request that the display and the machine may not idle-sleep, for as long as a film or
// a launched title runs. Returns the state now in effect; the previous flags are logged so a released hold
// proves the earlier request was live. Never changes a power plan or a setting.
bool holdDisplayAwake(bool hold);
// ---- Auto-Off (Phase 4, SESSION-POWER.md) ----
// The console's Auto-Off turns the display off and leaves the XMB running, so the next controller press wakes
// the panel back into the XMB instead of the desktop. Windows has no public "power the display off" API; the
// documented equivalent is a WM_SYSCOMMAND/SC_MONITORPOWER (2) broadcast, which asks the system's window
// defaults to power the panel down. It is a request, not a guarantee: a locked session, an RDP/virtual display,
// a policy or a vendor driver can ignore it, and this build never escalates to a privileged tool, a power-plan
// write or a simulated power button. Every attempt is logged. The only honest read-back Windows offers is the
// GUID_CONSOLE_DISPLAY_STATE notification: observeDisplayState() subscribes once, displayState() reports what
// was last observed, and requestDisplayOff() logs whether that state actually changed.
bool requestDisplayOff();      // one request per idle period; logs the execution state, the broadcast and the read-back
bool displayOffRequested();    // true while the display-off request stands (no activity, no display-on event)
void noteUserActivity();       // a press or a shell touch re-arms the next idle period; the display wakes on input
void observeDisplayState();    // subscribe to the console display state (once, at start-up or first request)
int displayState();            // last observed display state: 0 off, 1 on, 2 dimmed, -1 unknown/not observed
}
