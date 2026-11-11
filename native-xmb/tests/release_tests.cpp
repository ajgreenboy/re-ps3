#include "release_index.h"
#include "release_source.h"
#include "real_debrid.h"
#include "preferences.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

namespace {
int checks=0,failures=0;
void check(bool condition,const char* label) {
    ++checks;if(!condition){++failures;std::fprintf(stderr,"FAIL: %s\n",label);}
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    using namespace xmb;
    struct SizeCase { const char* label; double low; double high; double unit; };
    const double mb=1024.0*1024,gb=mb*1024;
    const SizeCase cases[]={
        {"2 GB",2,-1,gb},{"23.3 GB\n",23.3,-1,gb},{"13.3/14.7 GB",13.3,14.7,gb},
        {"4~4.3 GB",4,4.3,gb},{"13,3 GB",13.3,-1,gb},{"2,4 GB",2.4,-1,gb},
        {"19,9 GB",19.9,-1,gb},{"13,38 GB",13.38,-1,gb},{"215/374 MB",215,374,mb},
        {"8,8~9,5 GB SELECTIVE DOWNLOAD",8.8,9.5,gb},{"980 MB/1,3 GB (Selective Download)",980*mb/gb,1.3,gb},
        {"69 MB",69,-1,mb},{"1 KB",1,-1,1024},{"1 TB",1,-1,gb*1024},{"2 B",2,-1,1}};
    for(const auto& item:cases) {
        const auto size=parseSize(QString::fromLatin1(item.label));
        check(size.sizeBytes==qint64(std::round(item.low*item.unit)),item.label);
        check(size.sizeBytesMax==(item.high<0?-1:qint64(std::round(item.high*item.unit))),"size range max");
        check(size.sizeLabel==QString::fromLatin1(item.label),"original size label");
    }
    for(const auto& text:{QStringLiteral(" GB"),QStringLiteral("DIGIT NOT FOUND MB"),QStringLiteral("XX.VIII GB\n"),QStringLiteral("100"),QStringLiteral("999999999999999999 TB")})
        check(parseSize(text).sizeBytes==-1,"unknown/overflow size");
    check(parseSize(QJsonValue()).sizeBytes==-1,"absent size");
    const auto bare=parseMagnet(QStringLiteral("magnet:?xt=urn:btih:ABC012"));
    check(bare.infohash==QStringLiteral("abc012") && bare.trackers.isEmpty() && bare.displayName.isEmpty(),"bare magnet lowercase");
    const auto full=parseMagnet(QStringLiteral("MAGNET:?xt=urn:btmh:CaseSensitive&dn=Test+Game%20One&tr=https%3A%2F%2Fa.invalid&tr=udp%3A%2F%2Fb.invalid"));
    check(full.infohash==QStringLiteral("CaseSensitive") && full.displayName==QStringLiteral("Test Game One") && full.trackers.size()==2,"full v2 magnet");
    check(parseMagnet(QStringLiteral("magnet:?xt=urn:btmh:V2&xt=urn:btih:ABC")).infohash==QStringLiteral("abc"),"v1 preferred over v2");
    check(classifyUri(QStringLiteral("https://example.invalid/file.torrent?x=1"))==QStringLiteral("torrent"),"HTTP torrent priority");
    check(classifyUri(QStringLiteral("file.torrent"))==QStringLiteral("torrent"),"torrent");
    check(classifyUri(QStringLiteral("https://example.invalid/file"))==QStringLiteral("direct"),"direct");
    check(classifyUri(QStringLiteral("other"))==QStringLiteral("unknown"),"unknown");
    check(classifyUri(bare.uri)==QStringLiteral("magnet"),"magnet classification");
    check(asStringArray(QStringLiteral("one")).size()==1,"string array singleton");
    check(asStringArray(QJsonArray{1,QStringLiteral("two"),false}).size()==1,"string array filtering");
    check(asStringArray(QJsonValue()).isEmpty(),"absent string array");
    check(parseDate(QStringLiteral("2026-11-11T12:00:00Z")).isValid(),"ISO date");
    check(parseDate(QStringLiteral("Wed, 11 Nov 2026 12:00:00 +0000")).isValid(),"RFC date");
    check(parseDate(QJsonValue(1234567890.0)).toMSecsSinceEpoch()==1234567890,"millisecond date");
    check(!parseDate(QStringLiteral("not a date")).isValid(),"invalid date");
    const auto mirrors=groupToMirrors(QJsonArray{QJsonArray{QStringLiteral("https://example.invalid/game.part10.rar"),QStringLiteral("https://example.invalid/readme"),QStringLiteral("https://example.invalid/game.part2.rar")},QJsonArray{}});
    check(mirrors.size()==1 && mirrors.first().host==QStringLiteral("example.invalid") && mirrors.first().parts[0].uri.endsWith(QStringLiteral("part2.rar")) && mirrors.first().parts[1].uri.endsWith(QStringLiteral("readme")),"mirror numbered slots");
    const auto heuristic=QJsonDocument::fromJson(R"({"records":[{"label":"Example Game","size":"2 GB","links":["https://example.invalid/file"],"duplicate":"https://example.invalid/file"}]})");
    QString error;
    const auto guessed=parseReleaseIndex(heuristic,&error);
    check(error.isEmpty() && guessed.size()==1 && guessed.first().title==QStringLiteral("Example Game") && guessed.first().downloads.size()==1 && guessed.first().sizeBytes==qint64(2*gb),"heuristic adapter");
    const auto named=parseReleaseIndex(QJsonDocument::fromJson(R"([{"name":"Named Example","date":"2026-11-11T00:00:00Z","size":"215/374 MB"}])"));
    check(named.size()==1 && named.first().title==QStringLiteral("Named Example") && named.first().publishedAt.isValid() && named.first().sizeBytesMax==qint64(374*mb),"heuristic date and named title");
    if(argc>1) {
        QFile fixture(QString::fromLocal8Bit(argv[1]));check(fixture.open(QIODevice::ReadOnly),"fixture read");
        const auto releases=parseReleaseIndex(QJsonDocument::fromJson(fixture.readAll()),&error);
        check(error.isEmpty() && releases.size()==6,"FitGirl fixture six releases");
        for(const auto& release:releases)check(releaseToJson(releaseFromJson(releaseToJson(release)))==releaseToJson(release),"normalized roundtrip");
    }
    QTemporaryDir temporary;check(temporary.isValid(),"temp data directory");
    Preferences preferences(temporary.path());RealDebrid debrid(preferences);
    check(!debrid.hasToken(),"no configured token");
    debrid.unrestrict(QUrl(QStringLiteral("https://example.invalid/file")),&error);
    check(error==QStringLiteral("no Real-Debrid token configured"),"unrestrict missing token");
    debrid.addMagnet(bare.uri,&error);check(error==QStringLiteral("no Real-Debrid token configured"),"addMagnet missing token");
    debrid.torrentInfo(QStringLiteral("id"),&error);check(error==QStringLiteral("no Real-Debrid token configured"),"torrentInfo missing token");
    debrid.instantAvailability(QStringLiteral("abc"),&error);check(error==QStringLiteral("no Real-Debrid token configured"),"availability missing token");
    check(!debrid.selectFiles(QStringLiteral("id"),QStringLiteral("all"),&error) && error==QStringLiteral("no Real-Debrid token configured"),"selectFiles missing token");
    QTcpServer server;check(server.listen(QHostAddress::LocalHost,0),"local download server");
    QObject::connect(&server,&QTcpServer::newConnection,&app,[&] {
        auto* socket=server.nextPendingConnection();
        QObject::connect(socket,&QTcpSocket::readyRead,socket,[socket]{
            socket->readAll();socket->write("HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nfixture");socket->disconnectFromHost();
        });
        QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    });
    const QString direct=QStringLiteral("http://127.0.0.1:%1/test.zip").arg(server.serverPort());
    const QJsonObject raw{{QStringLiteral("name"),QStringLiteral("Local Test")},{QStringLiteral("downloads"),QJsonArray{
        QJsonObject{{QStringLiteral("title"),QStringLiteral("CON: Test/Game")},{QStringLiteral("uris"),direct}},
        QJsonObject{{QStringLiteral("title"),QStringLiteral("Magnet Only")},{QStringLiteral("uris"),bare.uri}}}}};
    const QString input=QDir(temporary.path()).filePath(QStringLiteral("index.json"));QFile file(input);
    check(file.open(QIODevice::WriteOnly),"source fixture open");file.write(QJsonDocument(raw).toJson());file.close();
    ReleaseSources store(temporary.path(),preferences);
    check(store.importSource(QUrl::fromLocalFile(input),&error) && error.isEmpty(),"local source import");
    const auto stored=store.listSources(&error);check(stored.size()==1 && stored.first().releases.size()==2 && error.isEmpty(),"offline source readback");
    if(!stored.isEmpty()) {
        check(store.downloadRelease(stored.first().slug,QStringLiteral("Magnet Only"),&error).isEmpty() && error==QStringLiteral("no Real-Debrid token configured"),"orchestration missing token");
        qint64 received=0;
        const auto path=store.downloadRelease(stored.first().slug,QStringLiteral("CON: Test/Game"),&error,[&](qint64 a,qint64){received=a;});
        QFile downloaded(path);check(!path.isEmpty() && error.isEmpty() && path.endsWith(QStringLiteral(".zip")) && downloaded.open(QIODevice::ReadOnly) && downloaded.readAll()==QByteArray("fixture") && received==7,"direct fallback streaming/progress/safe filename");
    }
    check(preferences.setValue(QStringLiteral("debrid/realDebridToken"),QStringLiteral("synthetic-test-value")),"token preference accepted");
    Preferences reloaded(temporary.path());check(reloaded.value(QStringLiteral("debrid/realDebridToken")).toString()==QStringLiteral("synthetic-test-value"),"token preference survives reload");
    std::printf("Release backend: %d checks, %d failures\n",checks,failures);return failures?2:0;
}
