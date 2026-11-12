#include "flare_solverr.h"
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace xmb {
bool FlareSolverr::request(const QUrl& url, QByteArray* response, QString* error) const {
    if(response)*response=QByteArray();
    if(endpoint_.isEmpty()) { if(error)*error=QStringLiteral("No FlareSolverr endpoint configured"); return false; }
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(endpoint_+QStringLiteral("/v1")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
    const QJsonObject body{{QStringLiteral("cmd"),QStringLiteral("request.get")},
        {QStringLiteral("url"),url.toString()},{QStringLiteral("maxTimeout"),60000}};
    QNetworkReply* reply=manager.post(request,QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    bool timedOut=false;
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{timedOut=true;reply->abort();});
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    timer.start(120000); loop.exec();
    if(timedOut){ reply->deleteLater(); if(error)*error=QStringLiteral("FlareSolverr timed out"); return false; }
    if(reply->error()!=QNetworkReply::NoError) {
        const QString message=reply->errorString(); reply->deleteLater();
        if(error)*error=QStringLiteral("FlareSolverr: %1").arg(message); return false;
    }
    const QJsonObject result=QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    if(result.value(QStringLiteral("status")).toString()!=QStringLiteral("ok")) {
        if(error)*error=QStringLiteral("FlareSolverr: %1").arg(result.value(QStringLiteral("message")).toString());
        return false;
    }
    QByteArray solved=result.value(QStringLiteral("solution")).toObject().value(QStringLiteral("response")).toString().toUtf8();
    if(solved.isEmpty()) { if(error)*error=QStringLiteral("FlareSolverr returned an empty response"); return false; }
    // FlareSolverr returns driver.page_source: a raw JSON/text response is wrapped in an
    // <html><body><pre>... page. Unwrap the <pre> and unescape the entities the source uses (&nbsp;
    // in size strings).
    if(solved.trimmed().startsWith('<')) {
        QString text=QString::fromUtf8(solved);
        const int pre=text.indexOf(QStringLiteral("<pre>"));
        if(pre>=0) {
            const int start=pre+5;
            const int end=text.indexOf(QStringLiteral("</pre>"),start);
            text=end>=0?text.mid(start,end-start):text.mid(start);
        }
        text.replace(QStringLiteral("&nbsp;"),QString(QChar(0x00A0)));
        text.replace(QStringLiteral("&lt;"),QStringLiteral("<"));
        text.replace(QStringLiteral("&gt;"),QStringLiteral(">"));
        text.replace(QStringLiteral("&quot;"),QStringLiteral("\""));
        text.replace(QStringLiteral("&#39;"),QStringLiteral("'"));
        text.replace(QStringLiteral("&amp;"),QStringLiteral("&"));
        solved=text.toUtf8();
    }
    if(solved.trimmed().isEmpty()) { if(error)*error=QStringLiteral("FlareSolverr returned an empty body"); return false; }
    if(response)*response=solved; return true;
}
} // namespace xmb
