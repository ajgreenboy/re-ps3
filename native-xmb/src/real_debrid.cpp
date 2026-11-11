#include "real_debrid.h"
#include "preferences.h"
#include "release_index.h"
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace xmb {
namespace {
void fail(QString* error,const QString& message) { if(error)*error=message; }
QByteArray formValue(const QString& key,const QString& value) {
    return QUrl::toPercentEncoding(key)+'='+QUrl::toPercentEncoding(value);
}
QString segment(const QString& text) { return QString::fromLatin1(QUrl::toPercentEncoding(text)); }
}
bool RealDebrid::hasToken() const {
    return !preferences_.value(QStringLiteral("debrid/realDebridToken")).toString().trimmed().isEmpty();
}
bool RealDebrid::request(const QString& path,const QByteArray* form,QJsonObject* result,QString* error) const {
    if(error)error->clear();
    const QString token=preferences_.value(QStringLiteral("debrid/realDebridToken")).toString().trimmed();
    if(token.isEmpty()){fail(error,QStringLiteral("no Real-Debrid token configured"));return false;}
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QStringLiteral("https://api.real-debrid.com/rest/1.0/")+path));
    request.setRawHeader("Authorization",QByteArray("Bearer ")+token.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply=form?manager.post(request,*form):manager.get(request);
    QEventLoop loop; QTimer timer; timer.setSingleShot(true); bool timedOut=false,tooLarge=false; QByteArray bytes;
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{timedOut=true;reply->abort();});
    QObject::connect(reply,&QNetworkReply::readyRead,&loop,[&]{bytes+=reply->readAll();if(bytes.size()>4*1024*1024){tooLarge=true;reply->abort();}});
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    timer.start(30000); loop.exec(); bytes+=reply->readAll();
    const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto document=QJsonDocument::fromJson(bytes); const auto object=document.object();
    if(timedOut){fail(error,QStringLiteral("Real-Debrid request timed out"));return false;}
    if(tooLarge){fail(error,QStringLiteral("Real-Debrid response too large"));return false;}
    if(object.contains(QStringLiteral("error")) || reply->error()!=QNetworkReply::NoError || status<200 || status>=300) {
        QString message=object.value(QStringLiteral("error")).toString();
        const auto details=object.value(QStringLiteral("error_details"));
        QString detail;
        if(details.isObject())detail=QString::fromUtf8(QJsonDocument(details.toObject()).toJson(QJsonDocument::Compact));
        else if(details.isArray())detail=QString::fromUtf8(QJsonDocument(details.toArray()).toJson(QJsonDocument::Compact));
        else if(!details.isNull() && !details.isUndefined())detail=details.toVariant().toString();
        if(!detail.isEmpty())message+=(message.isEmpty()?QString():QStringLiteral(": "))+detail;
        if(message.isEmpty())message=QStringLiteral("Real-Debrid request failed (HTTP %1)").arg(status);
        // Defend against an upstream error echoing the credential.
        message.replace(token,QStringLiteral("[redacted]")); fail(error,message);return false;
    }
    if(status!=204 && !document.isObject()){fail(error,QStringLiteral("Invalid Real-Debrid JSON response"));return false;}
    if(result)*result=object;
    return true;
}
QUrl RealDebrid::unrestrict(const QUrl& directLink,QString* error) const {
    QJsonObject object; const auto form=formValue(QStringLiteral("link"),directLink.toString(QUrl::FullyEncoded));
    if(!request(QStringLiteral("unrestrict/link"),&form,&object,error))return {};
    const QUrl url(object.value(QStringLiteral("download")).toString());
    if(!url.isValid() || url.host().isEmpty() || (url.scheme()!=QStringLiteral("http") && url.scheme()!=QStringLiteral("https"))) {
        fail(error,QStringLiteral("Real-Debrid returned no download link"));return {};
    }
    return url;
}
QString RealDebrid::addMagnet(const QString& magnet,QString* error) const {
    QJsonObject object; const auto form=formValue(QStringLiteral("magnet"),magnet);
    if(!request(QStringLiteral("torrents/addMagnet"),&form,&object,error))return {};
    const QString id=object.value(QStringLiteral("id")).toString();
    if(id.isEmpty())fail(error,QStringLiteral("Real-Debrid returned no torrent id"));
    return id;
}
DebridTorrentInfo RealDebrid::torrentInfo(const QString& id,QString* error) const {
    QJsonObject object;
    if(!request(QStringLiteral("torrents/info/")+segment(id),nullptr,&object,error))return {};
    DebridTorrentInfo result{object.value(QStringLiteral("status")).toString(),asStringArray(object.value(QStringLiteral("links"))),object.value(QStringLiteral("filename")).toString()};
    if(result.status.isEmpty())fail(error,QStringLiteral("Real-Debrid returned no torrent status"));
    return result;
}
QJsonObject RealDebrid::instantAvailability(const QString& infohash,QString* error) const {
    QJsonObject object; request(QStringLiteral("torrents/instantAvailability/")+segment(infohash),nullptr,&object,error);
    return object;
}
bool RealDebrid::selectFiles(const QString& id,const QString& files,QString* error) const {
    const auto form=formValue(QStringLiteral("files"),files);
    return request(QStringLiteral("torrents/selectFiles/")+segment(id),&form,nullptr,error);
}
}
