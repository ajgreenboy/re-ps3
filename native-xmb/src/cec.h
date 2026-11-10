#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// D9 part 2: HDMI-CEC TV control through Pulse-Eight's cec-client (libcec's command-line client), never bundled. The XMB
// starts cec-client with an argument list (no shell), writes the documented commands to its stdin and lets it exit:
//   cec-client -s -d 1        (-s single command mode, -d 1 log level ERROR only)
//   on 0   standby 0   as     (power the TV on, put the TV in standby, become the active source = switch the input)
// UNVERIFIED against hardware: this PC has no CEC adapter. The command text and the argument list are tested; the TV is not.
namespace xmb::cec {
enum class Action { TvOn, ActiveSource, Standby };

struct Availability {
    bool available=false;
    QString client;        // full path to cec-client(.exe) when found
    bool libcecFound=false;
    QString reason;        // why it is unavailable (empty when available)
};
// XMB_CEC_CLIENT=<path> overrides the search (a test stub); otherwise: beside this exe, PATH, then the Pulse-Eight
// installer's folders. libcec.dll is looked for in the same places and only changes the reason text.
Availability detect();

// Pure. The stdin text for a list of actions: "on 0\nas\n".
QByteArray commandText(const QVector<Action>& actions);
// Pure. The arguments cec-client is started with.
QStringList clientArguments();

// Starts cec-client for the actions. XMB_CEC_DRY_RUN=1 logs the command and does not start it (the rows then count as
// available, so the whole path is exercisable without an adapter). `wait` blocks up to timeoutMs (Turn Off must finish
// before Windows shuts down); otherwise it runs detached in the background. Returns false when unavailable or failed.
bool send(const QVector<Action>& actions,const QString& why,bool wait=false,int timeoutMs=8000);
bool dryRun();
}
