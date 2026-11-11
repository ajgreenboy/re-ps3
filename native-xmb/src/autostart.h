#pragma once
#include <QString>
#include <functional>

// D3, System Settings > Start at Sign-in. INFERRED: the firmware has no such row (the console has no sign-in).
// "On" means one thing, defined by packaging/install-autostart.ps1: a per-user scheduled task (name
// re-ps3-native-kiosk, or XMB_AUTOSTART_TASK for a dry run) whose action runs the watchdog.ps1 that sits next
// to this exe. The XMB never builds that task itself: it runs the script with an argument list, so the row and
// the installer share one definition. No administrator rights, no stored password, no shell string.
namespace xmb::autostart {
// The scheduled task's name: XMB_AUTOSTART_TASK when set, otherwise the installer's default.
QString taskName();
// False with a reason when this is not a packaged build (no watchdog.ps1 / install-autostart.ps1 beside the exe).
bool available(QString* reason=nullptr);
// The cached state of the task: exists and points at the watchdog.ps1 beside this exe. Never blocks; the first
// call (or prefetch(), at startup) reads it on a worker. Always false when !available().
bool enabled();
void prefetch();
// Creates (on) or removes (off) the task by running the installer script, then reads the task back. Blocks for
// the length of the PowerShell run; returns the read-back state through `error` when it differs from the request.
bool setEnabled(bool on,QString* error=nullptr);
// Asynchronous form of setEnabled(): the PowerShell run and the scheduler read-back happen on a worker thread
// (QtConcurrent), the cached state is updated there, and `done(ok, error)` is delivered back on the GUI thread
// (the main event loop) so a caller can update widgets directly. The blocking setEnabled() stays as the worker
// body and for any non-GUI caller; the two share one definition of the write and its read-back.
void setEnabledAsync(bool on,std::function<void(bool,QString)> done);
}
