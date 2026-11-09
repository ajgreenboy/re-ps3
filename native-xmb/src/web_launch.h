#pragma once
#include <QString>
#include <QStringList>

namespace xmb::webLaunch {
// Per-app launch style for kind "web" apps (apps.json field "window"):
//   "app"     chromeless Chromium window (--app=<url>), the default
//   "kiosk"   fullscreen Chromium kiosk (--kiosk)
//   "browser" the system default browser through QDesktopServices (the pre-P6 behaviour)
// Both Chromium styles run on a dedicated --user-data-dir under <data>/browser-profile, so the user's own
// browser profile and session are never read or touched, and the XMB tracks the process it started.
QString normalizeWindow(const QString& value);            // "" -> "app"; unknown -> empty
bool isValidWindow(const QString& value);                 // "", "app", "kiosk" or "browser"
// Installed Edge or Chrome executable, or empty. XMB_WEB_BROWSER=<exe> overrides the search (verification);
// XMB_WEB_BROWSER=none forces "not found" so the fallback path can be exercised.
QString findChromium();
QStringList arguments(const QString& executable,const QString& url,const QString& window,const QString& profileDir);
}
