#include "updater.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto check = [](bool ok, const char* message) { if(!ok) std::fprintf(stderr, "FAIL: %s\n", message); return ok; };
    QString why;
    bool ok = check(xmb::newerVersion(QStringLiteral("0.2.0"), QStringLiteral("v0.2.1"), &why), "patch")
        && check(!xmb::newerVersion(QStringLiteral("1.2"), QStringLiteral("v1.2.0"), &why) && why.isEmpty(), "missing component")
        && check(xmb::newerVersion(QStringLiteral("1.9"), QStringLiteral("2"), &why), "major")
        && check(!xmb::newerVersion(QStringLiteral("1"), QStringLiteral("v1.2-beta"), &why) && !why.isEmpty(), "invalid version");
    QTemporaryDir tmp; QFile file(tmp.filePath(QStringLiteral("fixture")));
    ok = check(tmp.isValid() && file.open(QIODevice::WriteOnly), "fixture creation") && ok;
    file.write("abc"); file.close();
    ok = check(xmb::verifyUpdateFile(file.fileName(), QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), &why), "SHA-256 match") && ok;
    ok = check(!xmb::verifyUpdateFile(file.fileName(), QString(64, QLatin1Char('0')), &why), "SHA-256 mismatch") && ok;
    xmb::Release release; release.assets.append({QStringLiteral("re-ps3.zip"), QString(), 0});
    release.assets.append({QStringLiteral("re-ps3.zip.sha256"), QString(), 0});
    ok = check(xmb::updateHashSource(release, xmb::updateAsset(release)) == QStringLiteral("sidecar"), "sidecar selection") && ok;
    release.body = QStringLiteral("SHA256: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ok = check(xmb::updateHashSource(release, xmb::updateAsset(release)) == QStringLiteral("body"), "body selection") && ok;
    const QString install = tmp.filePath(QStringLiteral("install")); QDir().mkdir(install);
    QFile exe(tmp.filePath(QStringLiteral("re-ps3-native.exe")));
    ok = check(exe.open(QIODevice::WriteOnly), "package fixture creation") && ok;
    exe.write("fixture"); exe.close();
    const QString zip = tmp.filePath(QStringLiteral("package.zip"));
    QProcess ps; ps.start(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), QStringLiteral("Compress-Archive -LiteralPath '%1' -DestinationPath '%2'").arg(exe.fileName(), zip)});
    ok = check(ps.waitForFinished(30000) && ps.exitCode() == 0, "create zip") && ok;
    QString staged;
    ok = check(xmb::applyZip(zip, install, &staged, &why), "stage zip") && ok;
    ok = check(QFile::exists(staged + QStringLiteral("/re-ps3-native.exe")) && QDir(install).exists(), "staging preserves install") && ok;
    if(ok) std::puts("Updater fixtures passed");
    return ok ? 0 : 1;
}
