# Session and power behaviour (HTPC)

Phase 4 of [PHASES-2026-10-27.md](../PHASES-2026-10-27.md). This document records what was implemented in
this pass, what the OS actually did under test, and - for files this pass did not own - the exact hunks that
are still required.

Write fence for this pass: `src/host.cpp`, `src/host.h`, `src/main.cpp`, `src/pad.cpp`, `src/pad.h` and this
file. `shell.*`, `view.*`, `settings_menu.cpp`, `preferences.cpp`, `jellyfin_*`, `CMakeLists.txt` and the
harness were read but not modified. Every hunk in this document is written against the tree as it stood on
2026-10-27; re-read before applying.

## 4.1 Auto-Off goes dark instead of quitting

### What the console does

The console's System Auto-Off powers the display down and leaves the system running: the console is still
on, the next controller press wakes the panel, and nothing is lost. The XMB never "quits to a desktop"
because a console has no desktop.

### What Windows actually offers

Windows has no public "turn the display off" API. The documented equivalent is a window message: a
`WM_SYSCOMMAND` with `SC_MONITORPOWER` and `lParam` 2 (off). This pass broadcasts it to every top-level
window (`HWND_BROADCAST`) and lets the default window procedure perform the power action. It is a request:

- A locked session, an RDP or virtual display, a managed/policy machine or a vendor driver can ignore it.
- `SC_MONITORPOWER` is legacy and its behaviour is not contractual; Microsoft never promoted a replacement.
- The app cannot prove the panel's power state. The only honest read-back is the
  `GUID_CONSOLE_DISPLAY_STATE` power notification (0 off, 1 on, 2 dimmed), which the implementation
  subscribes to and logs.

Nothing privileged is done: no power-plan write (`powercfg` is never called), no simulated power button, no
`SetSuspendState`, no scheduled task, no registry change. The request is sent only when the user's own
`power/idleMinutes` setting has elapsed; a key press or controller button ends it. Reversibility is the OS's
own: input wakes the panel, and the app also re-arms on the display-state notification or on a shell touch.

### Implemented

`src/host.h` / `src/host.cpp`:

- `bool requestDisplayOff()` - drops any outstanding display-awake hold (`SetThreadExecutionState(
  ES_CONTINUOUS)`), logs the previous execution state, broadcasts `WM_SYSCOMMAND`/`SC_MONITORPOWER` 2 with a
  1 s timeout, logs the result, and starts a 6 s read-back against `GUID_CONSOLE_DISPLAY_STATE`. Idempotent
  inside one idle period; a request older than 60 s can be repeated (so an ignored request can be retried on
  a later idle period).
- `bool displayOffRequested()` - the state the log and the capture sidecar report.
- `void noteUserActivity()` - re-arms the next idle period.
- `void observeDisplayState()` / `int displayState()` - subscribe once and report the last observed state.

`src/main.cpp` composition-root glue (needed because `shell.cpp` is owned elsewhere):

- `Shell` emits `requestQuit` from exactly two places today: the idle timer (`power/idleMinutes`) and the PS
  menu's confirmed "Turn Off the System". The confirmation emits while `Shell::psMenuConfirmingOff()` is still
  true, so that flag is the discriminator: a confirmed Turn Off quits, an idle Auto-Off becomes
  `host::requestDisplayOff()` and the event loop keeps running.
- The display-state observer is registered at start-up, before any request.
- New `--script` tokens: `pause=<ms>` (bounded 0-600000) lets real idle time pass inside a scripted run, and
  `F10` is the keyboard substitute for the PS button (so a script can open the PS menu without a physical pad
  or a foreground race with the global PS watcher).
- The `grab=` sidecar now carries `displayOffRequested`, `displayState`, `fullscreen`, `windowWidth` and
  `windowHeight`.

### Observed behaviour (measured 2026-10-27)

See "Verification results" below for the exact log lines. In summary: with `power/idleMinutes` = 1, the
scripted run crossed 60 s of idle time; the app logged `power: Auto-Off at idle 60992 ms; the XMB stays
running` and `power: display-off requested` instead of exiting, then Windows reported the display state as
off, and the process was still running when the after-frame was captured. Whether the panel really powered
down is reported only by the OS notification, quoted in the results section; a framebuffer capture cannot
prove the monitor's state.

### Shell hunk (still required)

This replaces the idle quit with the display-off request and re-arms on any input. It can land once
`shell.cpp` is free; the main.cpp glue above is then redundant inside the auto-off branch and can be deleted
(the explicit Turn Off path in the same connection stays).

```diff
--- a/src/shell.cpp
+++ b/src/shell.cpp
@@ -1243,7 +1243,12 @@
         const bool playing=mediaOpen_ && !photoOpen() && player_->playbackState()!=QMediaPlayer::StoppedState;
         const bool active=playing || gameRunning();
         if(active!=sessionHold_)sessionHold_=host::holdDisplayAwake(active);
         const int minutes=preferences_.value("power/idleMinutes").toInt();
-        // Never close the XMB while a title it launched is still running.
-        if(minutes && !mediaOpen_ && !gameRunning() && !QApplication::activeModalWidget() && activity_.elapsed()>=qint64(minutes)*60000)emit requestQuit();
+        // Never close the XMB while a title it launched is still running. Auto-Off goes dark, like the
+        // console's: ask Windows to power the display down and keep the XMB alive, so the next controller
+        // press wakes the TV back into the XMB instead of the desktop (audit/SESSION-POWER.md). The request
+        // is idempotent for one idle period; touch() re-arms it.
+        if(minutes && !mediaOpen_ && !gameRunning() && !QApplication::activeModalWidget() &&
+           activity_.elapsed()>=qint64(minutes)*60000)
+            host::requestDisplayOff();
     });
```

```diff
--- a/src/shell.cpp
+++ b/src/shell.cpp
@@ -2382,1 +2382,6 @@
-void Shell::touch(){idleTimeOverride_=-1;activity_.restart();}
+void Shell::touch(){
+    idleTimeOverride_=-1;activity_.restart();
+    // A press ends any Auto-Off period, so the next idle period may ask the display off again.
+    host::noteUserActivity();
+}
```

`shell.cpp` already includes `host.h`, so no include change is needed.

### Verification recipe

```powershell
$data = 'C:\dev\out\native-power\data-idle'
New-Item -ItemType Directory -Force $data | Out-Null
'{"version":1,"values":{"power/idleMinutes":1}}' | Set-Content "$data\preferences.json" -Encoding utf8
$env:XMB_LOG = 'C:\dev\out\native-power\idle.log'
Start-Process -PassThru -FilePath C:\dev\out\native-power\re-ps3-native.exe -ArgumentList @(
  '--dev-flash','C:\dev\rpcs3\bin\dev_flash','--data-dir',$data,'--start-windowed',
  '--script','grab=C:/dev/out/native-power/idle-before.png,pause=65000,grab=C:/dev/out/native-power/idle-after.png,pause=4000,quit')
```

The minimum `power/idleMinutes` the settings validation accepts is 1, so the shortest crossing is 60 s;
`pause=65000` covers it with margin. The version-1 preferences file is migrated on load to the current
profiles format (version 2) by `Preferences`; the setting survives the migration. The display may physically
power down during the test; input wakes it.

## 4.2 Kiosk / launcher mode

`--kiosk` is the appliance recipe and does exactly four things, all in `main.cpp`:

- a frameless (`Qt::FramelessWindowHint`) fullscreen window, on the primary screen by default or on screen
  index `n` with `--display <n>` (validated against `QGuiApplication::screens()`, so an out-of-range index
  is an error, not a silent fallback);
- starts at the XMB (`View::setIntroEnabled(false)`), because the cold-boot intro is a boot animation, not a
  launcher screen;
- ignores the stored `display/fullscreen` preference for this run and does not write window geometry back,
  so kiosk mode leaves no preference behind;
- the controller exit path is the existing PS hold -> system menu -> "Turn Off the System" -> Cross, which
  quits the app. No global hotkey, no shell replacement, no registry autostart write.

`--start-windowed` is the testing counterpart: it opens a normal window even when the stored preference is
fullscreen, and (like kiosk) does not write geometry back for that run. `--kiosk` and `--start-windowed`
together are rejected.

`--display` without `--kiosk` is rejected rather than silently ignored. `--script` keeps working in every
mode; kiosk changes the window, not the layout, so the canonical capture recipe is unchanged.

Starting with Windows is deliberately not implemented as a registry/scheduled-task write. The documented
recipe for an HTPC operator is a shortcut in `shell:startup` whose target is:

```
"C:\dev\out\native-power\re-ps3-native.exe" --dev-flash "C:\dev\rpcs3\bin\dev_flash" --kiosk
```

That is a user-level action, reversible by deleting the shortcut, and needs no privilege.

### Honest limits

- The window has no title bar in kiosk mode, but Windows still owns the session: Alt+Tab can reach the
  desktop. Blocking that needs a shell replacement, which this project deliberately avoids.
- A fullscreen window on a display whose resolution changes (TV mode switch) keeps its screen, but the
  layout fits the new size only on the next geometry change; measured 1920x1080 here.
- `--kiosk` does not itself start with Windows, and does not prevent the taskbar from flashing on focus
  changes on some Windows builds.

### Evidence

Recorded in "Verification results": the sidecar's `fullscreen`/`windowWidth`/`windowHeight`, the `kiosk:`
log line naming the chosen display, and a Win32 read of the process window rect and style (`WS_CAPTION`
clear).

## 4.3 Controller status: kind, connection, battery

### Evidence for the report layouts

The parse uses the controller's own input report, so no driver, Steam or XInput layer is involved. Battery
offsets are relative to the payload after the report id:

| Report | First byte | Payload starts | Battery byte | Meaning |
|---|---|---|---|---|
| DualSense USB | `0x01` | byte 1 | payload 52 | low nibble 0-10 capacity, high nibble 0 discharging / 1 charging / 2 full / 0xa,0xb,0xf error |
| DualSense Bluetooth | `0x31` | byte 2 | payload 52 | same byte, one extra Bluetooth byte in front |
| DualShock 4 USB | `0x01` | byte 1 | payload 29 | low nibble level, bit 4 = cable, level 11 = charge complete |
| DualShock 4 Bluetooth | `0x11` | byte 3 | payload 29 | two extra Bluetooth bytes in front |
| DualShock 3 | `0x01` | - | none | capacity lives in the `0xF2` feature report, not read here |

Sources checked this pass: SDL's `SDL_hidapi_ps5.c` (payload offsets 52/53 for `ucBatteryLevel` /
`ucConnectState`, `data[1]` for USB and `data[2]` for Bluetooth state packets) and `SDL_hidapi_ps4.c`
(payload offset 29 for `ucBatteryLevel`, `data[1]` / `data[3]` dispatch), plus the Linux `hid-playstation`
and `hid-sony` layouts quoted by those drivers. The transport is read from the report id, which is what the
controller actually sent; it is not inferred from a device path string.

### Implemented

`src/pad.h` / `src/pad.cpp`:

- `enum class Kind { None, Ds3, Ds4, DualSense }` and `enum class Transport { Unknown, Usb, Bluetooth }`.
- `struct Info { Kind kind; Transport transport; int batteryPercent; QString charging; }`.
- `bool info(Info&)` - the attached pad behind `latest()`, on the same 300 ms freshness window.
- `bool parseReport(Kind,const QVector<quint8>&,Info&)` - the exact watcher parser, for documented synthetic
  reports. The battery byte is read only when the report is long enough for that layout's offset; a short or
  unknown report is rejected.
- The watcher logs `pad: <kind> over <transport>, battery <n>% (<state>)` whenever the identity, transport
  or battery changes.
- Duplicate battery bytes are never invented: a DualShock 3 reports `battery not reported`.

`src/main.cpp`:

- `--pad-report <file>` starts the watcher, waits 2.5 s, writes `{source:"hid", attached, kind, connection,
  batteryPercent, charging}` and exits 0 when a pad reported (1 otherwise).
- `--pad-check <file>` runs five documented synthetic reports through `parseReport` (DualSense USB/BT,
  DualShock 4 USB/BT and a deliberately truncated DualSense) and writes `{source:"synthetic",
  verifiedHardware:false, cases:[...]}`.

### Shell/view hunk (still required)

`view.cpp` already includes `pad.h`. The PS menu's controller/charge panel and the PS-tap indicator are the
two places that currently print the console's placeholder `-`; replace their values with the report:

```diff
--- a/src/view.cpp
+++ b/src/view.cpp
@@ -1482,9 +1482,20 @@
         for(int i=0;i<4;++i) {
             const QRectF slot(157+i*26.,682,20,20);
             p.fillRect(slot,i==0?QColor(214,72,72):QColor(68,68,68));
             text(slot,QString::number(i+1),14,QColor(235,235,235),Qt::AlignCenter);
         }
+        // The attached pad as its own input report describes it (pad.h): kind, connection and battery.
+        // A pad that reported nothing in the last 300 ms keeps the console's own "no reading" dash.
+        pad::Info padInfo;
+        const bool padReported=pad::info(padInfo);
+        const QString padKind=padReported
+            ?pad::kindName(padInfo.kind)+" ("+pad::transportName(padInfo.transport)+")"
+            :QObject::tr("No controller");
+        const QString padCharge=!padReported?QStringLiteral("-")
+            :padInfo.batteryPercent<0?QObject::tr("Not reported")
+            :QString::number(padInfo.batteryPercent)+"%"+ (padInfo.charging.isEmpty()?QString()
+                :QString(" (")+padInfo.charging+")");
         text({260,675,140,32},QObject::tr("Controller"),20,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
-        text({400,675,220,32},"1",20,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
+        text({400,675,560,32},padKind,20,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
         text({260,707,140,32},QObject::tr("Charge Level"),20,QColor(215,215,215),Qt::AlignLeft|Qt::AlignVCenter);
-        text({400,707,220,32},"-",20,QColor(215,215,215),Qt::AlignLeft|Qt::AlignVCenter);
+        text({400,707,560,32},padCharge,20,QColor(215,215,215),Qt::AlignLeft|Qt::AlignVCenter);
         drawImage(p,"system_plugin","tex_cross",{856,858,26,26});text({890,850,140,40},QObject::tr("Enter"),21,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
```

```diff
--- a/src/view.cpp
+++ b/src/view.cpp
@@ -1495,10 +1495,16 @@
     else if(psHudUntil_>0 && clock_.elapsed()<psHudUntil_) {
         // A PS tap flashes the controller indicator beside the clock, then fades.
         const double fade=std::clamp(double(psHudUntil_-clock_.elapsed())/600.,0.,1.);
         p.save();p.setOpacity(fade);
         drawImage(p,"system_plugin","tex_controller",{1230,92,34,34});
         text({1270,88,150,42},"1",22,Qt::white,Qt::AlignLeft|Qt::AlignVCenter);
-        text({1430,88,90,42},"-",22,QColor(210,210,210),Qt::AlignRight|Qt::AlignVCenter);
+        // The tap indicator shows the same battery the PS menu's charge panel reads.
+        pad::Info padInfo;
+        const QString charge=pad::info(padInfo) && padInfo.batteryPercent>=0
+            ?QString::number(padInfo.batteryPercent)+"%":QStringLiteral("-");
+        text({1430,88,90,42},charge,22,QColor(210,210,210),Qt::AlignRight|Qt::AlignVCenter);
         p.restore();
     }
```

If the panel is to become its own page instead of a PS-menu panel, the model should be a `Shell` accessor
(`Shell::controllerStatus()` returning the same `pad::Info`) rather than view code calling `pad::info()`
directly; the data already exists in `pad.h`, so that is a rendering-only change.

## 4.4 Safe area and UI scale (design hunk only)

The layout is a fixed 1920x1080 scene fitted to the window (`view.cpp` paintGL: `scale = min(pixels.width()/
1920, pixels.height()/1080)`, then centred). Two preferences extend that fit without touching any layout
constant.

**Preference keys**

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `display/safeArea` | int | 0-10 | 0 | percent of each edge a TV may crop; the whole 1920x1080 scene is fitted inside the surviving area |
| `display/uiScale` | int | 75-125 | 100 | percent scale of the fitted scene, about the display centre |

**Validation (`preferences.cpp`)** - add beside the other integer keys:

```diff
--- a/src/preferences.cpp
+++ b/src/preferences.cpp
@@ -40,6 +40,8 @@
     if(key=="media/repeatMode")return integer(0,2);
     if(key=="power/idleMinutes")return integer(0,120);
+    if(key=="display/safeArea")return integer(0,10);
+    if(key=="display/uiScale")return integer(75,125);
     if(key=="display/screensaverMinutes")return integer(0,30) && (value.toInt()==0 || value.toInt()==5 || value.toInt()==10 || value.toInt()==20 || value.toInt()==30);
```

and in `defaultValue`:

```diff
--- a/src/preferences.cpp
+++ b/src/preferences.cpp
@@ -22,1 +22,3 @@
-    if(key=="power/idleMinutes" || key=="display/screensaverMinutes")return 0;
+    if(key=="power/idleMinutes" || key=="display/screensaverMinutes" || key=="display/safeArea")return 0;
+    if(key=="display/uiScale")return 100;
```

**Choices (`settings_menu.cpp`, `choices()`)**:

```diff
--- a/src/settings_menu.cpp
+++ b/src/settings_menu.cpp
@@ -48,1 +48,3 @@
     if(key=="display/screensaverMinutes")return {{"After 5 Minutes",5},{"After 10 Minutes",10},{"After 20 Minutes",20},{"After 30 Minutes",30},{"Do Not Use",0}};
+    if(key=="display/safeArea")return {{"None",0},{"2%",2},{"5%",5},{"8%",8},{"10%",10}};
+    if(key=="display/uiScale")return {{"75%",75},{"80%",80},{"90%",90},{"100%",100},{"110%",110},{"125%",125}};
```

**Display Settings rows (`settings_menu.cpp`, `id=="display"`)**:

```diff
--- a/src/settings_menu.cpp
+++ b/src/settings_menu.cpp
@@ -115,6 +115,8 @@
         page.entries={option("display_output_setting",tr("msg_display_output_setting","Video Output Settings"),tr("msg_display_output_setting_explanation",{}),{},Action::Information,"host:display"),
             option("tvtype",tr("msg_tvtype","TV Type"),tr("msg_tvtype_explanation",{}),{},Action::Unavailable),
+            option("safearea","Safe Area","Insets the XMB for a TV that crops the edge of the picture. The background still fills the display.","display/safeArea"),
+            option("uiscale","UI Scale","Scales the XMB layout for couch distance. The background still fills the display.","display/uiScale"),
             option("screensaver","Screensaver","Dims the native display after inactivity.","display/screensaverMinutes"),
```

**Shell accessors (`shell.h`)**:

```diff
--- a/src/shell.h
+++ b/src/shell.h
@@ -131,6 +131,8 @@
     bool fullscreen() const { return preferences_.value("display/fullscreen").toBool(); }
+    int safeAreaPercent() const { return preferences_.value("display/safeArea").toInt(); }
+    int uiScalePercent() const { return preferences_.value("display/uiScale").toInt(); }
     bool photoFill() const { return preferences_.value("photo/fit").toInt()==1; }
```

**View transform (`view.cpp` paintGL)** - one change at the UI layer's fit:

```diff
--- a/src/view.cpp
+++ b/src/view.cpp
@@ -772,8 +772,14 @@
         // Glyphs are rasterised for the layer's own scale (twice the window): at the window's scale they were
         // magnified 2x by bilinear sampling before the 2x2 resolve, which left all text soft.
-        textRenderer_.beginFrame(*this,pixels,std::min(pixels.width()/1920.,pixels.height()/1080.),2);
+        // Overscan and UI scale (audit/SESSION-POWER.md 4.4): the whole 1920x1080 scene is fitted inside the
+        // safe rectangle (safeArea percent off each edge), then scaled about the centre. Both default to
+        // 0/100, which reproduces the old transform exactly. drawScene's inverted-transform wallpaper still
+        // fills the whole window, so only the UI insets - the picture never shows bars.
+        const double safe=std::clamp(shell_.safeAreaPercent(),0,10)/100.;
+        const double fit=std::min(pixels.width()/1920.,pixels.height()/1080.)*(1.-2.*safe)*
+            (std::clamp(shell_.uiScalePercent(),75,125)/100.);
+        textRenderer_.beginFrame(*this,pixels,fit,2);
         {
             QOpenGLPaintDevice paintDevice(pixels);
             QPainter ui(&paintDevice);
             ui.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
-            const double scale=std::min(pixels.width()/1920.,pixels.height()/1080.);
+            const double scale=fit;
             ui.translate((pixels.width()-1920*scale)/2,(pixels.height()-1080*scale)/2);ui.scale(scale,scale);drawScene(ui);
```

Also add the two values to the UI-layer signature (the transform is applied outside `drawScene`, and a
preference change must rebuild the layer even if animation is still):

```diff
-    signature+=shell_.clockText()+shell_.message()+shell_.status();
+    signature+=shell_.clockText()+shell_.message()+shell_.status();
+    signature+=QString::asprintf(" display %d %d",shell_.safeAreaPercent(),shell_.uiScalePercent());
```

The PS-menu, dialogs, OSK and media controls are all drawn inside `drawScene`, so they inherit both
settings; the screensaver dim is composited after the UI at the full device size, so it is unaffected.

## 4.5 PIN gate (design only)

No implementation in this pass: every surface it needs is in `shell.*`/`view.*`/`settings_menu.cpp`, which
this pass did not own.

**Storage.** One preference key, `security/pin`, whose value is a JSON string (the same mechanism
`media/resume` already uses):

```json
{"version":1,"algorithm":"PBKDF2-HMAC-SHA256","iterations":210000,
 "salt":"<16 random bytes, base64>","digest":"<32 bytes, base64>"}
```

The salt comes from `QRandomGenerator::system()`; the key is derived with
`QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,pinUtf8,salt,iterations,32)`, compared in
constant time. Validation in `preferences.cpp` is bounded: version 1, iterations 100000-500000, salt 16
bytes, digest 32 bytes, total string length <= 512. The preference file stays local to `<data>`; nothing is
sent anywhere. No plaintext PIN and no hash is ever logged, put in a sidecar, or included in the UI
signature.

**Surfaces protected.**

- Opening any Settings page that can write a preference (System, Sound, Display, Theme, Date and Time,
  Power Save, Photo/Music/Video settings, Accessory).
- "Restore Default Settings" in System Settings (otherwise it is a bypass).
- Optionally the PS menu's "Turn Off the System" confirmation in kiosk mode, so the household cannot exit
  the appliance; this is a separate toggle (`security/pinLocksExit`) because it is a different intent.
- Purchases are not applicable: this build has no in-app purchase surface (the Steam store row opens the
  Steam client/browser). If one is added, it must call the same gate before the handoff.
- Not protected: Jellyfin sign-in (it writes no preference and the password is already masked and only in
  memory), media playback, photo viewing.

**Behaviour.**

- With `security/pin` set, the gated action first opens the existing masked OSK (`Osk::masked`, the Jellyfin
  password path) through a generalised signal; a new `Shell::requestPin(title)` / `pinEntered(bool)` pair
  keeps the reason and the retry count in the shell.
- A correct PIN unlocks the settings area for 5 minutes or until the user leaves it, so changing three rows
  asks once, not three times.
- Wrong entries are rate-limited in memory: 5 failures -> 30 s lockout, each failure logged as
  `pin: rejected (attempt n)` with no value. The lockout resets on restart; it is a couch barrier, not an
  anti-forensics control.
- Setting, changing or clearing the PIN requires the current PIN (or, when none is set, an explicit
  confirmation).

**Recovery.** There is no recovery question, e-mail or backdoor: the only reset is deleting
`security/pin` from `<data>/preferences.json` on the desktop (documented in the install guide, where the
data directory is already documented). If the operator wants that to be harder, DPAPI-protect the whole
preferences file later; that is a separate change.

**Threat model / honest limits.** A 4-digit PIN has 10,000 combinations and the stored hash is offline
brute-forceable in seconds on any PC; PBKDF2 at 210k iterations only slows a casual attempt. The gate
protects against a housemate with the controller - changing settings, restoring defaults, exiting kiosk mode.
It does not protect the data directory, Jellyfin credentials, or the Windows account from someone with file
access. Use 6-8 digits if the threat includes an offline attacker, and say so in the UI (the settings row's
note).

**Follow-ups.** A `pin.cpp`/`pin.h` helper plus a CMake entry (CMake is owned elsewhere), the `shell.h`
accessors, two scripted harness states (locked/unlocked), and the DPAPI option for the preferences file.

## Files touched

| File | Change |
|---|---|
| `src/host.h` | Auto-Off API: `requestDisplayOff`, `displayOffRequested`, `noteUserActivity`, `observeDisplayState`, `displayState` |
| `src/host.cpp` | `SC_MONITORPOWER` broadcast, execution-state clear, `GUID_CONSOLE_DISPLAY_STATE` observation, bounded read-back, logging |
| `src/main.cpp` | `--kiosk`, `--display <n>`, `--start-windowed`, `--pad-report`, `--pad-check`, `pause=<ms>` and `F10` script tokens, auto-off glue, sidecar fields |
| `src/pad.h` | `Kind`, `Transport`, `Info`, `info()`, `parseReport()`, name helpers |
| `src/pad.cpp` | Battery/transport parse for DualSense (payload 52) and DualShock 4 (payload 29), bounded; identity/battery change log; NOMINMAX |
| `audit/SESSION-POWER.md` | this document |

## Verification results

Measured on 2026-10-27 against `C:\dev\out\native-power\re-ps3-native.exe` (built from the shared tree by
`native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-power`; the build printed `Built
C:\dev\out\native-power\re-ps3-native.exe`). Evidence files are under `C:\dev\out\native-power\`.

Environment note: other agents were building and running the same application at the same time, and blanket
`Stop-Process re-ps3-native` cleanups killed one long idle run. The three long runs below used a byte-identical
copy of the final executable under a distinct file name (`xmb-power-idle.exe`, `xmb-power-kiosk.exe`,
`xmb-power-wake.exe`) so that a cleanup by process name could not end the run. The process command lines and
the logs are otherwise the real build.

### 4.1 Auto-Off: the idle run

Scratch data directory with `power/idleMinutes` = 1, script `grab=before,pause=65000,grab=after,pause=4000,quit`:

- before frame at +8 s: `displayOffRequested=false`, `displayState=1`, `idleMilliseconds=6255`;
- process still alive at +71 s (`alive: True`) - it had not quit when the 60 s Auto-Off fired;
- after frame at +73 s, taken while the display was already off:
  `displayOffRequested=true`, `displayState=0`, `idleMilliseconds=71944`;
- the app then quit normally at the script's own `quit` token (exit code 0).

Log lines, in order:

```
power: observing the console display state (GUID_CONSOLE_DISPLAY_STATE)
power: console display state is now 1 (on)
script: grab C:/dev/out/native-power/idle-before.png QSize(960, 540) saved
script: pause 65000
power: Auto-Off at idle 60992 ms; the XMB stays running
power: display-off requested (Auto-Off; execution state 0x80000000 cleared)
power: console display state is now 0 (off)
power: SC_MONITORPOWER broadcast delivered (reply 0x0)
power: read-back 6 s after the display-off request: display state off
power: displayOffRequested stays true until activity or a display-on event
script: grab C:/dev/out/native-power/idle-after.png QSize(960, 540) saved
script: pause 4000
```

What the OS actually did: the broadcast was delivered, and Windows reported `GUID_CONSOLE_DISPLAY_STATE` 0
(off) within the same second, so the panel really was powered down by the request. The application kept
rendering while dark (the after frame is a normal 960x540 XMB frame), which is the point of Auto-Off: only the
panel sleeps, not the process. The capture itself cannot prove the panel's power state; the notification is
the evidence, and the state-0 line above is what Windows reported.

Reversibility probe (same build, `--script pause=12000,quit`, display off from the previous run):

```
power: console display state is now 0 (off)
power: console display state is now 1 (on)      <- after a WM_SYSCOMMAND/SC_MONITORPOWER -1 broadcast
power: console display state is now 0 (off)     <- host power plan re-timed it 4 s later, no input
```

So the panel does come back on the documented `-1` request in this session, and the host's own display
timeout then turns it off again because the agent session generates no input. In normal use the user's own
key press is the wake: it is input, it resets the host idle timer, and `Shell::touch()` -> the new
`host::noteUserActivity()` re-arms Auto-Off for the next idle period. The implementation never disables the
input path while dark.

The explicit PS-menu exit still quits: `--script F10,wait,ENTER,wait,ENTER` exited with code 0 and logged
`power: Turn Off the System confirmed; the XMB quits` (the second ENTER is the confirmation).

### 4.2 Kiosk

`--kiosk --display 1 --script grab=...,pause=15000,quit`:

- log: `kiosk: frameless fullscreen on display 1 (KG241Q P), 1920x1080`;
- Win32 window rect: `-1920,4,0,1084` = 1920x1080, exactly the `\\.\DISPLAY1` screen bounds
  (`-1920,4 1920x1080`); the primary display is `\\.\DISPLAY2` at `0,0`;
- window style `0x96080000`: `WS_CAPTION` clear, `WS_THICKFRAME` clear, `WS_POPUP` set - no title bar;
- framebuffer grab `kiosk.png` is 1920x1080 and shows the XMB (Game column, Steam Store focused);
- sidecar: `fullscreen=true`, `windowWidth=1920`, `windowHeight=1080`;
- `data-kiosk\settings.ini` is absent after exit: kiosk did not write window geometry;
- process alive at the grab, exit code 0.

`--start-windowed` against a stored `display/fullscreen=true`: `fullscreen=false`, 960x540; the same data
directory without the flag: `fullscreen=true`, 1920x1080.

### 4.3 Controller status

The machine had a DualSense attached over USB (`USB\VID_054C&PID_0CE6`). `--pad-report`:

```json
{"source":"hid","verifiedHardware":true,"attached":true,"kind":"DualSense","connection":"USB",
 "batteryPercent":100,"charging":"full"}
```

and the watcher's own line in the application log: `pad: DualSense over USB, battery 100% (full)`.

`--pad-check` (documented synthetic reports, marked `verifiedHardware:false`):

| case | bytes | parsed | batteryRead | kind / connection | battery |
|---|---|---|---|---|---|
| dualsense-usb | 64 | true | true | DualSense / USB | 75% discharging |
| dualsense-bluetooth | 78 | true | true | DualSense / Bluetooth | 100% full |
| dualshock4-usb | 64 | true | true | DualShock 4 / USB | 75% charging |
| dualshock4-bluetooth | 78 | true | true | DualShock 4 / Bluetooth | 65% discharging |
| dualsense-truncated | 40 | true | false | DualSense / USB | not reported (-1) |

The last row is the bound: the 40-byte report parses its buttons, but the battery byte at payload 52 is never
read, so the level stays -1 instead of reading past the report.

The PS-menu panel hunk above has not been applied (view.cpp is owned elsewhere), so the screenshot still
shows the console's placeholder "1" and "-" in that panel. The data behind the hunk is what the runs above
prove.

### 4.4 Safe area / UI scale

Design hunk only (settings_menu.cpp, preferences.cpp and view/shell are owned elsewhere); no capture was
taken. Applying it is a rendering-only change gated by the existing canonical capture, which defaults to
safeArea 0 / uiScale 100 and therefore reproduces today's frame exactly.

### 4.5 PIN gate

Design only; no implementation in this pass.

### Canonical capture gate

`bun work\ab-layers.ts --capture --exe C:/dev/out/native-power/re-ps3-native.exe`:

```
frame              0,0,1920,1080      18.28   18.02   +0.26  0.8288  0.8301  -0.0013  PASS
top background     0,0,700,200        20.22   20.24   -0.01  0.9336  0.9344  -0.0008  PASS
category icon row  0,240,1920,120     20.89   21.05   -0.16  0.8966  0.8950  +0.0016  PASS
item label row     400,425,800,100    11.94   11.94   +0.00  0.3909  0.3910  -0.0001  PASS
clock text         1570,80,260,50     11.39   11.39   +0.00  0.4105  0.4105  +0.0000  PASS
info bar           1329,75,591,58     14.78   14.78   +0.00  0.6117  0.6117  +0.0000  PASS

PASS: 6/6 layers within budget
```

Kiosk mode changes the window, not the layout: the same `--script full,wait,wait,wait,grab=...,quit` recipe
still reproduces the canonical frame with no `--kiosk` argument.

### Known limits of this evidence

- The display power state comes from Windows' `GUID_CONSOLE_DISPLAY_STATE` notification, not from a
  measurement of the panel's backlight. No framebuffer capture can prove a monitor is off.
- `SC_MONITORPOWER` behaviour is not contractual. A locked session, an RDP/virtual display or a policy may
  ignore it; the 6 s read-back line is then a warning, and the XMB still stays alive (which is the part the
  app controls).
- The controller panel hunk, the safe-area transform and the PIN gate are designs here because their files
  were owned by other changes in this pass.
