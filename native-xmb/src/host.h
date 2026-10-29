#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
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

// ==== The settings source layer (Phase: settings pages) ============================================
// Every Settings row shows either a value this computer really reports or a value the app really stores,
// and a row that changes something changes it where that is safe and reversible. This section is the
// machine half: it reads the host through Win32 and Qt, owns no window, and never invents a value. A
// read that a standard user token cannot perform says so (status "requires-administrator" and the
// privilege named in `note`); a read the platform does not expose at all says that too, rather than
// guessing. `--settings-report` prints the whole structure as JSON.

// ---- elevation (Win32 token check; nothing is requested or changed) -------------------------------
bool elevated();                 // the process holds an elevated (administrator) token
QString elevationText();         // one line: "elevated (administrator token)" or "not elevated (standard user)"
void logElevation();             // the one-line start-up log entry, once per process

// One settings row's live value.
struct Reading {
    QString text;      // the value column's string; empty only for a row that is an action with no value
    QString source;    // win32 | qt | com | registry | wmi | derived | app | none
    QString status;    // action | live | read-only | requires-administrator | unavailable | not-applicable
    QString note;      // why a value is absent or read-only, never a placeholder
    bool ok=false;     // `text` is a real value this pass could read (or the row is an action)
    QJsonObject toJson() const;
};
// The value a settings row shows, by the row's own stable id (the ids settings_menu.cpp builds the pages
// with). An empty Reading (text empty, source "none") means the row's value is the app's own preference:
// the caller resolves that through its store, so the two layers never both claim the same row.
Reading rowValue(const QString& pageId,const QString& rowId);
// The body of the row's information page: real headings and values, or the reason a write is withheld.
// Empty when the row has no page of its own beyond the firmware's explanation.
QString rowDetail(const QString& pageId,const QString& rowId);

// ---- the console's "Settings and Connection Status List" (SETTINGS 1.18) ---------------------------
// The 13 rows of that read-only table, in the console's order, each with the value this computer really
// reports. `ok` is false for a row with no honest host source and `note` says why; the report prints all
// 13, so an empty value is visible instead of dropped (AT-20). Two of the labels ("Address Settings",
// "UPnP") are measured on the reference frame rather than read from an RCO string, because the local
// string tables do not carry them.
struct StatusRow {
    QString id,label,value,source,note;
    bool ok=false;
};
QVector<StatusRow> networkStatusList(const Label& label);
// The Internet Connection Test row's result. `probe` also opens one bounded TCP connection to a public
// resolver (1.2 s) so the answer is about reachability and not only about the interface; it writes no
// configuration, changes no adapter and is not part of any capture path (SETTINGS 4/AT-21).
QJsonObject networkTest(bool probe=true);

// The console choice rows that write a real Windows setting (Accessory's Key Repeat Interval/Speed, Mouse
// Type and Pointer Speed). The index is the item's position in the firmware's own list; these map it to the
// platform setting and back, so a row's value and the write cannot disagree. hostChoiceIndex() is -1 for a
// key this layer does not serve. setHostChoice() logs old and new value, is reversible from the same list and
// needs no administrator rights.
int hostChoiceIndex(const QString& key);
bool setHostChoice(const QString& key,int index);
QString hostChoiceName(const QString& key);

// ---- the machine ---------------------------------------------------------------------------------
struct Volume {
    QString label, mount, filesystem, kind;   // label "Windows (C:)", kind "Fixed"/"Removable"
    qint64 totalBytes=0, freeBytes=0;
    bool removable=false, readOnly=false, system=false;
};
struct AdapterInfo {
    QString name, description, mac, kind, ssid;   // kind "Ethernet"/"Wireless"/"Other"
    bool up=false, wireless=false, dhcp=false;
    qint64 linkSpeed=0;
    int signalQuality=-1, mtu=0, prefixLength=0;
    QString subnet;                 // the IPv4 mask the host reports for the first address
    QStringList ipv4, ipv6, gateways, dns;
    QString dhcpServer, dhcpLease;
};
struct DisplayInfo {
    QString name, monitor, adapter, deviceId;
    QSize resolution;
    double refresh=0, dpi=0;
    int depth=0, bitsPerPixel=0;
    bool primary=false;
    QString edid;      // decoded EDID identity when the monitor's key is readable
};
struct AudioInfo {
    QString name, adapter, id;
    bool isDefault=false;
    int channels=0, sampleRate=0, bits=0;
};
struct BatteryInfo {
    bool present=false, acOnline=false;
    int percent=-1;
    QString state;     // "discharging" | "charging" | "full" | "not charging" | "no battery"
    qint64 secondsLeft=-1;
};
struct PowerTimer {
    int acMinutes=-1, dcMinutes=-1;
};
struct PrinterInfo {
    QString name, driver, port;
    bool isDefault=false, offline=false;
};
struct PadInfo {
    QString name, transport;   // "USB" | "Bluetooth"
    QString vendor, product;   // "Sony" / the HID product string
    bool wireless=false;
    int batteryPercent=-1;
    QString charging;
};
struct KeyboardInfo {
    QString layoutName, layoutId, layoutLocale;
    int repeatDelay=0, repeatSpeed=0;   // raw SPI_GETKEYBOARDDELAY (0..3) / SPI_GETKEYBOARDSPEED (0..31)
    int installed=0;                    // layouts Windows has for this user
};
struct MouseInfo {
    bool swapped=false;
    int speed=0, buttons=0;             // SPI_GETMOUSESPEED (1..20), SM_CMOUSEBUTTONS
};
struct ClockInfo {
    QString timeZoneId, timeZoneName, standardName, daylightName;
    QString localeName, language, country, datePattern, timePattern;
    int utcOffsetSeconds=0;
    bool daylight=false;
    QDateTime now;
    bool timeServicePresent=false, timeServiceRunning=false, automatic=false;
    QString autoDetail;
};
struct Snapshot {
    qint64 takenAtMs=0;
    ClockInfo clock;
    QString hostName, osName, osVersion, osBuild, architecture, kernel;
    QString cpuName, cpuClock, gpuName, gpuDriver, gpuMemory, motherboard, bios;
    int cpuCores=0, cpuLogical=0;
    quint64 ramTotal=0, ramFree=0;
    QString windowsAccount, windowsDisplayName, windowsSid, windowsDomain;
    bool windowsAdmin=false;
    QVector<Volume> volumes;
    QVector<AdapterInfo> adapters;
    QVector<DisplayInfo> displays;
    QVector<AudioInfo> audioOutputs, audioInputs;
    QStringList cameras;
    QVector<PrinterInfo> printers;
    QString defaultPrinter;
    QVector<PadInfo> pads;
    BatteryInfo battery;
    PowerTimer monitorTimer, sleepTimer;
    QString powerScheme;
    KeyboardInfo keyboard;
    MouseInfo mouse;
    int keyboards=0, mice=0;
    int volume=-1;
    bool muted=false;
    QString opticalDrive;          // first optical drive's letter and media, empty when the PC has none
    QString defaultBrowser;
    QString dlnaService;
    QString dataDirectory;
    QString dataDirectorySize;
    QString appVersion, qtVersion, renderer;
};
// The machine as it is now, cached for a couple of seconds so a settings page can be drawn without a
// syscall per row (the volume row alone would otherwise open a Core Audio endpoint sixty times a second).
const Snapshot& snapshot();
void invalidateSnapshot();           // after a write this layer or the shell performed
// The running app's data directory, so the backup row and the report can name (and size) the one piece of
// state this build really keeps. Never written; the caller passes its own.
void setDataDirectory(const QString& path);
QJsonObject snapshotJson(const Snapshot& state);   // the whole structure, for --settings-report
// Read-only capability lines a caller can print instead of guessing why a value is absent.
QStringList privilegeNotes();
}
