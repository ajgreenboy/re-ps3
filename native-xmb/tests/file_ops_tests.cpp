#include "file_ops.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

// Delete and Copy refusals for paths that reach outside the library through a directory junction or a
// symbolic link (release run 2026-11-12, bug 2). Only scratch folders are touched, and every Delete
// below is refused before it reaches the Recycle Bin. Build with -DXMB_FILE_OPS_BUILD_TESTS=ON;
// exit code 0 = all pass. Pass a scratch parent folder as the first argument to keep the test off %TEMP%.
using xmb::FileOperations;

static bool touch(const QString& path,const QByteArray& body=QByteArrayLiteral("x")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(body)==body.size();
}
static bool junction(const QString& link,const QString& target) {
#ifdef Q_OS_WIN
    return QProcess::execute(QStringLiteral("cmd"),{QStringLiteral("/c"),QStringLiteral("mklink"),QStringLiteral("/J"),
        QDir::toNativeSeparators(link),QDir::toNativeSeparators(target)})==0;
#else
    return QFile::link(target,link);
#endif
}
static bool unlinkJunction(const QString& link) {
#ifdef Q_OS_WIN
    return QDir().rmdir(link);   // removes the junction only, never the target's content
#else
    return QFile::remove(link);
#endif
}
// Runs one Delete request to its answer.
static bool deleteAnswer(FileOperations& ops,const QStringList& paths,QString* message) {
    QEventLoop loop;
    bool ok=true;
    QObject::connect(&ops,&FileOperations::finished,&loop,[&](quint64,bool good,QString text,QStringList){ok=good;*message=text;loop.quit();});
    ops.deleteToRecycleBin(paths);
    loop.exec();
    return ok;
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    int failures=0;
    const auto check=[&](bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);++failures;}};
    const QString parent=argc>1?QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1])):QDir::tempPath();
    QTemporaryDir scratch(parent+QStringLiteral("/xmb-fileops-XXXXXX"));
    if(!scratch.isValid()){std::fprintf(stderr,"cannot create scratch folder\n");return 2;}
    const QString base=QDir::cleanPath(scratch.path());
    const QString library=base+QStringLiteral("/library"),outside=base+QStringLiteral("/outside"),elsewhere=base+QStringLiteral("/other-library");
    check(touch(library+QStringLiteral("/inside.mp4")),"scratch file in library");
    check(touch(library+QStringLiteral("/sub/deep.mp4")),"scratch file in a library subfolder");
    check(touch(outside+QStringLiteral("/escaped.mp4")),"scratch file outside the library");
    check(touch(outside+QStringLiteral("/sub/escaped2.mp4")),"scratch file in a folder outside the library");
    check(touch(elsewhere+QStringLiteral("/other.mp4")),"scratch file in a second library root");
    const QString link=library+QStringLiteral("/link");
    const bool haveLink=junction(link,outside);
    check(haveLink,"junction created");

    FileOperations ops;
    ops.setAllowedRoots({library,elsewhere});
    check(ops.checkDeletable(library+QStringLiteral("/inside.mp4")).isEmpty(),"a file in the library may be deleted");
    check(ops.checkDeletable(library+QStringLiteral("/sub/deep.mp4")).isEmpty(),"a file in a library subfolder may be deleted");
    check(ops.checkDeletable(elsewhere+QStringLiteral("/other.mp4")).isEmpty(),"a file in the second library root may be deleted");
    check(!ops.checkDeletable(outside+QStringLiteral("/escaped.mp4")).isEmpty(),"a file outside the library is refused");
    if(haveLink) {
        const QString file=link+QStringLiteral("/escaped.mp4");
        check(QFileInfo::exists(file),"the file is visible through the junction");
        const QString why=ops.checkDeletable(file);
        std::fprintf(stdout,"junction file Delete check: '%s'\n",qPrintable(why));
        check(why.startsWith(QStringLiteral("Refused: ")),"a file reached through a junction to outside is refused");
        check(!ops.checkDeletable(link+QStringLiteral("/sub/escaped2.mp4")).isEmpty(),"a file in a folder below the junction is refused");
        check(!ops.checkDeletable(link).isEmpty(),"the junction itself is refused");
        QString message;
        check(!deleteAnswer(ops,{file},&message) && message.startsWith(QStringLiteral("Refused: ")),"Delete answers Refused for the junction path");
        check(QFileInfo::exists(outside+QStringLiteral("/escaped.mp4")),"the file outside the library is still there after Delete");
        check(!deleteAnswer(ops,{library+QStringLiteral("/inside.mp4"),file},&message) && message.startsWith(QStringLiteral("Refused: ")),"Delete Multiple is refused as a whole when one path escapes");
        check(QFileInfo::exists(library+QStringLiteral("/inside.mp4")) && QFileInfo::exists(outside+QStringLiteral("/escaped.mp4")),"nothing was touched by the refused Delete Multiple");

        // Copy: a source reached through the junction, and a destination behind it.
        check(!ops.checkCopy({file},elsewhere).isEmpty(),"Copy of a file that resolves outside the library is refused");
        check(!ops.checkCopy({library+QStringLiteral("/inside.mp4")},link).isEmpty(),"Copy into a folder that resolves outside the library is refused");
        check(ops.checkCopy({library+QStringLiteral("/inside.mp4")},elsewhere).isEmpty(),"Copy between two library roots is allowed");
        check(ops.checkCopy({library+QStringLiteral("/sub/deep.mp4")},library).isEmpty(),"Copy inside the library is allowed");
        check(unlinkJunction(link),"junction removed without touching the target");
        check(QFileInfo::exists(outside+QStringLiteral("/escaped.mp4")),"target content survives junction removal");
    }

    // A library root that is itself a junction to another folder keeps working (its resolved form is a root too).
    const QString realLibrary=base+QStringLiteral("/real-library");
    check(touch(realLibrary+QStringLiteral("/movie.mp4")),"scratch file in the real library");
    const QString rootLink=base+QStringLiteral("/library-link");
    if(junction(rootLink,realLibrary)) {
        FileOperations viaLink;
        viaLink.setAllowedRoots({rootLink});
        check(viaLink.checkDeletable(rootLink+QStringLiteral("/movie.mp4")).isEmpty(),"a library root that is a junction still allows its files");
        check(viaLink.checkCopy({rootLink+QStringLiteral("/movie.mp4")},rootLink).isEmpty(),"Copy within a junction library root is allowed");
        unlinkJunction(rootLink);
    } else check(false,"root junction created");

    std::fprintf(stdout,failures?"file ops tests: %d FAILED\n":"file ops tests: all passed\n",failures);
    return failures?1:0;
}
