#pragma once

// Command-line surface of the XMB Manager. manager_main.cpp calls
// isCliInvocation() before deciding whether to create a QApplication/UI; when
// any verb is present the process runs headless (QGuiApplication) and returns
// runCli()'s exit code.
//
// Exit codes: 0 success, 1 operation failed (including an inconsistent
// --validate), 2 usage or configuration error.

#include <QString>
#include <QStringList>

namespace xmb::manager {

bool isCliInvocation(const QStringList& arguments);

QString cliUsage();

int runCli(const QStringList& arguments);

} // namespace xmb::manager
