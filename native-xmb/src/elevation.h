#pragma once
#include <QString>
#include <QtGlobal>

// Is a window's process running as administrator (ZF1, audit/PHASES-ZERO-FRICTION-2026-11-11.md section 6)?
//
// Windows' UIPI stops a lower-integrity process from sending input to a higher-integrity window, so SendInput from an XMB that is
// not elevated reaches nothing while an elevated window has focus. This only asks the question: OpenProcess with
// PROCESS_QUERY_LIMITED_INFORMATION, OpenProcessToken with TOKEN_QUERY and GetTokenInformation(TokenElevation), none of which needs
// elevation. It never changes a token, a setting or a UAC policy, and nothing in the XMB injects input on an elevated process's
// behalf (there is deliberately no helper; see the plan's section 6).
namespace xmb::elevation {
// A failed query is Unknown, never Elevated.
enum class State { NotElevated, Elevated, Unknown };
const char* name(State state);

State processState(quint32 pid);
State windowState(quintptr hwnd);     // the window's owning process
State selfState();                     // this process, cached after the first call
// True when input aimed at a window in `target` would be dropped by UIPI: the target is elevated and the XMB is not.
// Unknown never blocks (the caller still tries, and the old "delivered nothing" log stays as the fallback).
bool blocksDelivery(State target);
bool blocksDelivery(State target,State self);

// The wording, one place. The full sentence goes in the log and the docs; the pop-up is two lines of at most about 30 characters
// each (the plaque is 400 px wide and elides, see View::drawNotification), so it carries the short pair.
QString fullMessage();
QString noticeTitle();
QString noticeDetail();
}
