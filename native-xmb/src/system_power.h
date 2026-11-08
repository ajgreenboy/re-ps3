#pragma once
// ==== system_power: real Sleep / Hibernate / Restart / Shut Down for the XMB (plan item H1) ===========
//
// INTEGRATION NOTE (for the shell owner; nothing below is wired in yet)
//
// Which XMB rows call what:
//   * Users column "Turn Off System" (shell.cpp, Action::TurnOffSystem) and the PS-button menu's
//     "Turn Off the System" (shell.cpp psMenu rows): both already reach the one psConfirmOff_ confirmation
//     and then Shell::requestQuit. The confirmed branch in main.cpp (requestQuit with
//     shell.psMenuConfirmingOff() true) is the single call site: replace `app.quit()` there with
//         QString err; if(!xmb::requestShutdown(&err)) { qWarning()<<"power:"<<err; app.quit(); }
//     On success do NOT quit immediately: Windows sends WM_QUERYENDSESSION/WM_ENDSESSION and Qt closes the
//     app as part of the shutdown; quitting first is harmless but loses nothing either way. On failure fall
//     back to today's behaviour (quit to the desktop) and show the error text.
//     The console's Turn Off is a power-off, so the PS3 mapping is requestShutdown(), not Sleep.
//   * Settings > Power Save Settings (settings_menu.cpp page "power"): add action rows below System Auto-Off,
//     each shown only when available:  "Sleep" -> requestSleep(false)   (row hidden when !canSleep())
//                                      "Hibernate" -> requestSleep(true) (row hidden when !canHibernate())
//                                      "Restart" -> requestRestart()
//                                      "Turn Off Display" -> turnDisplayOff(nullptr, window->winId())
//                                      "Lock" -> lockWorkstation() (optional; on a kiosk HTPC with autologin
//                                                 the lock screen needs a keyboard/PIN to leave)
//     These rows are not firmware rows (the console's Power Save page only has the two Auto-Off timers);
//     mark them as host extensions, like the other host rows.
//
// Confirmation the XMB must show first (reuse the existing confirm box):
//   * Shut Down: the existing "Turn Off the System?" Yes/No (already shown, keep it).
//   * Restart:   "Restart the system?" Yes/No, default focus on No.
//   * Sleep / Hibernate: "Put the system to sleep?" / "Hibernate the system?" Yes/No, default No.
//   * Turn Off Display and Lock are reversible: no confirmation needed.
//   Every request here acts at once: there is no grace period, no countdown and no cancel after the call.
//   Shut Down and Restart are NOT forced: applications get WM_QUERYENDSESSION and may block to save work, so
//   a running game with unsaved data can hold the shutdown on Windows' "close apps" screen.
//
// Testing: set XMB_POWER_DRY_RUN=1 (or call setPowerDryRun(true)) and every request below logs
// "power: <action> (dry run)" and returns true without touching the OS. canSleep()/canHibernate() and
// enableShutdownPrivilege() are always real (read-only / process-token-only, nothing shuts down).
// Windows only; on other platforms every request fails with "not supported on this platform".
#include <QString>

namespace xmb {

// ---- dry run ---------------------------------------------------------------------------------------
// True when XMB_POWER_DRY_RUN is set to a non-empty value other than "0" in the environment (read once,
// on first use), or after setPowerDryRun(true). setPowerDryRun overrides the environment either way.
bool powerDryRun();
void setPowerDryRun(bool on);

// ---- capabilities (always real; GetPwrCapabilities) ------------------------------------------------
// canSleep: the firmware offers a classic sleep state (S1, S2 or S3) that SetSuspendState can enter.
// A Modern Standby (S0 low power idle, "AoAc") machine without S3 returns false: SetSuspendState(FALSE)
// does not enter Modern Standby; the closest equivalent there is turnDisplayOff().
bool canSleep();
// canHibernate: S4 supported and a hibernation file present (powercfg /hibernate on).
bool canHibernate();
// One line for logs and an information page: "S1 no, S2 no, S3 yes, S4 yes (hiberfile yes), S5 yes,
// modern standby no, hybrid sleep ...".
QString powerCapabilitiesText();

// ---- privilege --------------------------------------------------------------------------------------
// Enables SE_SHUTDOWN_NAME on this process token (AdjustTokenPrivileges, then GetLastError must be
// ERROR_SUCCESS, not ERROR_NOT_ALL_ASSIGNED). Called by the requests below; public so a test can prove the
// adjustment works without shutting anything down. Enabling the privilege changes nothing on the machine.
bool enableShutdownPrivilege(QString* error=nullptr);

// ---- requests (each acts immediately; see the confirmation rules above) -----------------------------
// SetSuspendState(hibernate, force=FALSE, wakeEventsDisabled=FALSE). Returns when the machine resumes, or
// at once with false. Fails up front (no OS call) when the matching can*() is false.
bool requestSleep(bool hibernate,QString* error=nullptr);
// InitiateShutdownW(nullptr, nullptr, 0 s grace, SHUTDOWN_RESTART / SHUTDOWN_POWEROFF,
//                   SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED). Not forced.
// Returns true when Windows accepted the request (the shutdown then proceeds asynchronously).
bool requestRestart(QString* error=nullptr);
bool requestShutdown(QString* error=nullptr);
// Powers the display down (SC_MONITORPOWER 2). With `window` (a top-level HWND as WId, e.g. the XMB's
// window->winId()) the message goes to that one window, whose default window procedure performs it; no other
// application sees it. Without one it falls back to a HWND_BROADCAST with SendMessageTimeout
// (SMTO_ABORTIFHUNG, 1 s) so a hung window cannot block the XMB. Either way Windows reports success of the
// delivery only; whether the panel actually turned off is not observable here (host::displayState() is).
bool turnDisplayOff(QString* error=nullptr,quintptr window=0);
// LockWorkStation(): asynchronous; true means the lock was requested.
bool lockWorkstation(QString* error=nullptr);

} // namespace xmb
