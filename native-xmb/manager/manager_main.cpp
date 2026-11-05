// xmb-manager entry point.
//
// CLI verbs run headless (QGuiApplication); the Qt Widgets UI is created only
// when no verb is present (QApplication). The UI header belongs to the parallel
// UI pass; this file builds and links without it and reports a clear error, so
// the core can be verified before manager_ui.h/.cpp land.

#include "manager_cli.h"
#include "manager_core.h"

#include <QGuiApplication>
#include <QString>
#include <QStringList>

#if defined(__has_include)
#  if __has_include("manager_ui.h")
#    define XMB_MANAGER_HAS_UI 1
#  endif
#endif

#ifdef XMB_MANAGER_HAS_UI
#include "manager_ui.h"

#include <QApplication>
#include <QMessageBox>
#endif

#include <cstdio>

namespace {

QStringList processArguments(int argc, char** argv) {
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));
    return arguments;
}

QString dataDirectoryOption(const QStringList& arguments) {
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString& argument = arguments.at(index);
        if (argument == QStringLiteral("--data-dir") && index + 1 < arguments.size())
            return arguments.at(index + 1);
        if (argument.startsWith(QStringLiteral("--data-dir=")))
            return argument.mid(QStringLiteral("--data-dir=").size());
    }
    return QString();
}

void configureApplication() {
    QCoreApplication::setApplicationName(QStringLiteral("xmb-manager"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("re-ps3-native"));
}

int runUi(int argc, char** argv, const QStringList& arguments) {
    const QString dataDirectory = dataDirectoryOption(arguments);

#ifdef XMB_MANAGER_HAS_UI
    QApplication app(argc, argv);
    configureApplication();
    xmb::manager::Settings settings;
    QString error;
    if (!xmb::manager::loadSettings(settings, error)) {
        QMessageBox::critical(nullptr, QStringLiteral("XMB Manager"), error);
        return 2;
    }
    if (!dataDirectory.isEmpty()
        && !xmb::manager::setDataDirectory(settings, dataDirectory, error, true)) {
        QMessageBox::critical(nullptr, QStringLiteral("XMB Manager"), error);
        return 2;
    }
    if (!xmb::manager::ensureDataLayout(settings, error)) {
        QMessageBox::critical(nullptr, QStringLiteral("XMB Manager"), error);
        return 2;
    }
    xmb::manager::MainWindow window(settings);
    window.show();
    return app.exec();
#else
    QGuiApplication app(argc, argv);
    configureApplication();
    xmb::manager::Settings settings;
    QString error;
    if (!xmb::manager::loadSettings(settings, error)) {
        xmb::manager::logError(error);
        return 2;
    }
    if (!dataDirectory.isEmpty()) {
        if (!xmb::manager::setDataDirectory(settings, dataDirectory, error, true)) {
            xmb::manager::logError(error);
            return 2;
        }
    }
    xmb::manager::logError(
        QStringLiteral("This build has no UI: manager_ui.h was not present when xmb-manager was "
                       "built. Build after the UI pass adds manager/manager_ui.h and "
                       "manager/manager_ui.cpp, or use a CLI command (--help)."));
    return 2;
#endif
}

} // namespace

int main(int argc, char** argv) {
    const QStringList arguments = processArguments(argc, argv);
    if (xmb::manager::isCliInvocation(arguments)) {
        QGuiApplication app(argc, argv);
        configureApplication();
        return xmb::manager::runCli(arguments);
    }
    // An option that is not a known verb, `--data-dir` or `--ui` is almost always a typo
    // (`--list-emulator` instead of `--list-emulators`); opening the GUI silently hides it.
    // Treat it as a CLI usage error (exit 2) like the CLI verbs do.
    bool unknownOption = false;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString& argument = arguments.at(index);
        if (argument == QStringLiteral("--ui") || argument == QStringLiteral("--data-dir")
            || argument.startsWith(QStringLiteral("--data-dir=")))
            continue;
        if (argument.startsWith(QLatin1Char('-'))) {
            unknownOption = true;
            break;
        }
    }
    if (unknownOption) {
        std::fprintf(stderr, "%s\n", qPrintable(xmb::manager::cliUsage()));
        std::fprintf(stderr, "xmb-manager: unrecognized option; run --help for the verb list.\n");
        return 2;
    }
    return runUi(argc, argv, arguments);
}
