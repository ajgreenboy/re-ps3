#include "steam_friends.h"

#include "steam_owned.h"
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <memory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <utility>

namespace xmb {
namespace {
constexpr int RequestTimeoutMs=8000;
constexpr qsizetype MaxResponseBytes=2*1024*1024;
constexpr int MaxFriends=1000;
constexpr int SummaryBatchSize=100;

bool validSteamId(const QString& value) {
    static const QRegularExpression digits(QStringLiteral("^[0-9]{1,20}$"));
    if(!digits.match(value).hasMatch())return false;
    bool ok=false;
    const qulonglong numeric=value.toULongLong(&ok,10);
    return ok && numeric!=0;
}

bool validApiKey(const QString& value) {
    static const QRegularExpression keyPattern(QStringLiteral("^[A-Fa-f0-9]{32}$"));
    return keyPattern.match(value).hasMatch();
}

int sortBucket(const SteamFriend& friendInfo) {
    if(!friendInfo.game.isEmpty())return 0;
    if(friendInfo.personaState!=0)return 1;
    return 2;
}
}

SteamFriends::SteamFriends(QObject* parent):QObject(parent),network_(new QNetworkAccessManager(this)) {
    qRegisterMetaType<xmb::SteamFriend>();
    qRegisterMetaType<xmb::SteamFriendList>();
}

SteamFriends::~SteamFriends() { ++generation_;cancelRequests(); }

bool SteamFriends::isCurrent(quint64 generation) const { return generation==generation_; }

void SteamFriends::cancelRequests() {
    if(clientProcess_) {
        auto* process=clientProcess_;clientProcess_=nullptr;
        process->kill(); // Its finished handler releases it, without blocking the render thread.
    }
    const auto pending=replies_;
    replies_.clear();
    for(auto* reply:pending)if(reply)reply->abort();
}

void SteamFriends::refresh(QString steamId) {
    ++generation_;
    const quint64 generation=generation_;
    cancelRequests();
    friendIds_.clear();
    summaryIds_.clear();
    friends_.clear();
    nextSummary_=0;key_.clear();source_.clear();clientError_.clear();
    if(!busy_){busy_=true;emit busyChanged(true);}
    const QString id=steamId.trimmed();
    if(!id.isEmpty() && !validSteamId(id)){fail(generation,QStringLiteral("Steam ID must be a nonzero numeric 64-bit ID."));return;}
    if(tryClient(generation,id))return;
    startWeb(generation,id);
}

void SteamFriends::startWeb(quint64 generation,const QString& id) {
    if(!isCurrent(generation))return;
    source_=QStringLiteral("steam-web-api");
    if(!validSteamId(id)) {
        if(busy_){busy_=false;emit busyChanged(false);}
        emit loaded({},id.isEmpty()?QStringLiteral("No Steam account is selected. Sign in to Steam, or set XMB_STEAM_ID to your numeric Steam ID and restart XMB."):
            QStringLiteral("Steam ID must be a nonzero numeric 64-bit ID."));
        return;
    }
    const QString key=SteamOwned::apiKey();
    if(key.isEmpty()) {
        if(busy_){busy_=false;emit busyChanged(false);}
        emit loaded({},clientError_.isEmpty()?QStringLiteral("Connect the signed-in Steam client, or configure the optional Web API key."):QStringLiteral("Steam client connection: ")+clientError_);
        return;
    }
    if(!validApiKey(key)) {
        if(busy_){busy_=false;emit busyChanged(false);}
        emit loaded({},QStringLiteral("STEAM_WEB_API_KEY has an invalid format. Use the 32-character key from Steam's Web API key page."));
        return;
    }

    key_=key;
    if(!busy_){busy_=true;emit busyChanged(true);}
    requestFriendList(generation,id,key_);
}

bool SteamFriends::available() {
    const bool helper=qEnvironmentVariable("XMB_STEAM_WEB_ONLY")!="1" &&
        QFileInfo(QDir(QCoreApplication::applicationDirPath()).filePath("xmb-steam-client.exe")).isFile();
    return helper || SteamOwned::hasApiKey();
}

void SteamFriends::finishFriends() {
    std::sort(friends_.begin(),friends_.end(),[](const SteamFriend& left,const SteamFriend& right){
        const int a=sortBucket(left),b=sortBucket(right);
        if(a!=b)return a<b;
        const int order=QString::compare(left.name,right.name,Qt::CaseInsensitive);
        return order?order<0:left.steamId<right.steamId;
    });
    busy_=false;key_.clear();emit busyChanged(false);emit loaded(friends_,{});
}

bool SteamFriends::tryClient(quint64 generation,const QString& steamId) {
    if(qEnvironmentVariable("XMB_STEAM_WEB_ONLY")=="1")return false;
    const QString helper=QDir(QCoreApplication::applicationDirPath()).filePath("xmb-steam-client.exe");
    if(!QFileInfo(helper).isFile()){clientError_="The Steam client helper is not installed in this build.";return false;}
    QString root=qEnvironmentVariable("XMB_STEAM_ROOT");
    if(root.isEmpty())root=QSettings("HKEY_CURRENT_USER\\Software\\Valve\\Steam",QSettings::NativeFormat).value("SteamPath").toString();
    if(root.isEmpty()){clientError_="Steam is not installed in the configured location.";return false;}
    auto* process=new QProcess(this);clientProcess_=process;
    auto environment=QProcessEnvironment::systemEnvironment();environment.remove("STEAM_WEB_API_KEY");
    process->setProcessEnvironment(environment);
    auto output=std::make_shared<QByteArray>();auto done=std::make_shared<bool>(false);
    auto timedOut=std::make_shared<bool>(false);auto oversized=std::make_shared<bool>(false);
    const auto complete=[this,process,output,done,timedOut,oversized,generation,steamId](int code,QProcess::ExitStatus status){
        if(*done)return;*done=true;
        if(clientProcess_==process)clientProcess_=nullptr;
        output->append(process->readAllStandardOutput());
        process->deleteLater();
        if(!isCurrent(generation))return;
        QString error;
        QJsonObject result;
        if(*timedOut)error="The signed-in Steam client did not respond in time.";
        else if(*oversized || output->size()>MaxResponseBytes)error="The Steam client response exceeded the supported size.";
        else {
            const QByteArray marker="XMB_STEAM_FRIENDS:";const auto pos=output->lastIndexOf(marker);
            QJsonParseError parse{};
            const auto document=QJsonDocument::fromJson(pos<0?QByteArray{}:output->mid(pos+marker.size()).split('\n').first(),&parse);
            if(parse.error!=QJsonParseError::NoError || !document.isObject())error="The Steam client helper returned no valid result.";
            else {result=document.object();error=result.value("error").toString().left(1024);}
            if(error.isEmpty() && (code!=0 || status!=QProcess::NormalExit))error="The Steam client helper could not complete the connection.";
        }
        SteamFriendList people;
        if(error.isEmpty()) {
            const QString owner=result.value("steamId").toString();const auto rows=result.value("friends");
            if(!validSteamId(owner) || (!steamId.isEmpty() && owner!=steamId) || !rows.isArray() || rows.toArray().size()>4096)
                error="The Steam client returned an unexpected account or friends list.";
            else {
                QSet<QString> ids;
                for(const auto& value:rows.toArray()) {
                    const auto row=value.toObject();SteamFriend person;
                    person.steamId=row.value("steamId").toString();person.name=row.value("name").toString();
                    person.game=row.value("game").toString();person.personaState=row.value("personaState").toInt(-1);
                    if(!validSteamId(person.steamId) || person.name.isEmpty() || person.name.size()>256 || person.game.size()>256 ||
                       person.personaState<0 || person.personaState>6 || ids.contains(person.steamId)) {error="The Steam client returned invalid friend data.";break;}
                    ids.insert(person.steamId);people.push_back(std::move(person));
                }
            }
        }
        if(!error.isEmpty()){clientError_=error;startWeb(generation,steamId);return;}
        friends_=std::move(people);source_="steam-client";finishFriends();
    };
    connect(process,&QProcess::readyReadStandardOutput,this,[process,output,oversized]{
        output->append(process->readAllStandardOutput());
        if(output->size()>MaxResponseBytes){*oversized=true;process->kill();}
    });
    connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,complete);
    connect(process,&QProcess::errorOccurred,this,[complete](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)complete(-1,QProcess::CrashExit);
    });
    QTimer::singleShot(RequestTimeoutMs,process,[process,done,timedOut]{if(!*done){*timedOut=true;process->kill();}});
    QStringList arguments{"--steam-root",root};if(!steamId.isEmpty())arguments<<"--expected-user"<<steamId;
    process->start(helper,arguments);
    return true;
}

void SteamFriends::requestFriendList(quint64 generation,const QString& steamId,const QString& key) {
    QUrl url(QStringLiteral("https://api.steampowered.com/ISteamUser/GetFriendList/v1/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("steamid"),steamId);
    query.addQueryItem(QStringLiteral("relationship"),QStringLiteral("friend"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("x-webapi-key",key.toUtf8());
    request.setTransferTimeout(RequestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    auto* reply=network_->get(request);
    replies_.push_back(reply);
    reply->setReadBufferSize(MaxResponseBytes+1);
    auto* deadline=new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);
    deadline->start(RequestTimeoutMs);
    connect(reply,&QNetworkReply::downloadProgress,this,[this,reply,generation](qint64 received,qint64){
        if(isCurrent(generation) && received>MaxResponseBytes)reply->abort();
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation,key]{
        replies_.removeAll(reply);
        const auto bytes=reply->readAll();
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError=reply->error();
        reply->deleteLater();
        if(!isCurrent(generation))return;
        if(bytes.size()>MaxResponseBytes) {
            fail(generation,QStringLiteral("Steam returned a response larger than the supported limit."));
            return;
        }
        if(status==401 || status==403) {
            fail(generation,QStringLiteral("Steam did not authorize this Web API friends request (HTTP %1). Use the signed-in Steam client connection.").arg(status)+(clientError_.isEmpty()?QString{}:QStringLiteral("\n\nSteam client: ")+clientError_));
            return;
        }
        if(networkError!=QNetworkReply::NoError || status<200 || status>=300) {
            fail(generation,QStringLiteral("Steam could not return the friends list. Check the network and API key, then refresh."));
            return;
        }
        QJsonParseError parseError{};
        const auto document=QJsonDocument::fromJson(bytes,&parseError);
        const auto friendsValue=document.object().value(QStringLiteral("friendslist")).toObject()
                                    .value(QStringLiteral("friends"));
        const auto list=friendsValue.toArray();
        if(parseError.error!=QJsonParseError::NoError || !document.isObject() || !friendsValue.isArray() || list.size()>MaxFriends) {
            fail(generation,QStringLiteral("Steam returned an invalid or oversized friends list."));
            return;
        }
        friendIds_.reserve(list.size());
        for(const auto& item:list) {
            const QString id=item.toObject().value(QStringLiteral("steamid")).toString();
            if(validSteamId(id) && !friendIds_.contains(id))friendIds_.push_back(id);
        }
        if(friendIds_.isEmpty()) {
            busy_=false;
            key_.clear();
            emit busyChanged(false);
            emit loaded({},{});
            return;
        }
        requestSummaries(generation,key);
    });
}

void SteamFriends::requestSummaries(quint64 generation,const QString& key) {
    if(!isCurrent(generation))return;
    if(nextSummary_>=static_cast<int>(friendIds_.size())) {
        finishFriends();
        return;
    }

    const int friendCount=static_cast<int>(friendIds_.size()); // Friend-list parsing caps this at MaxFriends.
    const int end=std::min(nextSummary_+SummaryBatchSize,friendCount);
    QStringList ids;
    ids.reserve(end-nextSummary_);
    for(int i=nextSummary_;i<end;++i)ids.push_back(friendIds_[i]);
    nextSummary_=end;

    QUrl url(QStringLiteral("https://api.steampowered.com/ISteamUser/GetPlayerSummaries/v2/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("steamids"),ids.join(','));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("x-webapi-key",key.toUtf8());
    request.setTransferTimeout(RequestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    auto* reply=network_->get(request);
    replies_.push_back(reply);
    reply->setReadBufferSize(MaxResponseBytes+1);
    auto* deadline=new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);
    deadline->start(RequestTimeoutMs);
    connect(reply,&QNetworkReply::downloadProgress,this,[this,reply,generation](qint64 received,qint64){
        if(isCurrent(generation) && received>MaxResponseBytes)reply->abort();
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation,key]{
        replies_.removeAll(reply);
        const auto bytes=reply->readAll();
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError=reply->error();
        reply->deleteLater();
        if(!isCurrent(generation))return;
        if(bytes.size()>MaxResponseBytes) {
            fail(generation,QStringLiteral("Steam returned a response larger than the supported limit."));
            return;
        }
        if(networkError!=QNetworkReply::NoError || status<200 || status>=300) {
            fail(generation,QStringLiteral("Steam could not return player summaries. Check the network and API key, then refresh."));
            return;
        }
        QJsonParseError parseError{};
        const auto document=QJsonDocument::fromJson(bytes,&parseError);
        const auto playersValue=document.object().value(QStringLiteral("response")).toObject()
                                   .value(QStringLiteral("players"));
        const auto players=playersValue.toArray();
        if(parseError.error!=QJsonParseError::NoError || !document.isObject() || !playersValue.isArray() || players.size()>SummaryBatchSize) {
            fail(generation,QStringLiteral("Steam returned invalid player summaries."));
            return;
        }
        for(const auto& item:players) {
            const auto player=item.toObject();
            const QString id=player.value(QStringLiteral("steamid")).toString();
            if(!validSteamId(id) || !friendIds_.contains(id) || summaryIds_.contains(id))continue;
            summaryIds_.insert(id);
            SteamFriend entry;
            entry.steamId=id;
            entry.name=player.value(QStringLiteral("personaname")).toString().trimmed();
            if(entry.name.isEmpty())entry.name=id;
            entry.game=player.value(QStringLiteral("gameextrainfo")).toString().trimmed();
            entry.personaState=std::clamp(player.value(QStringLiteral("personastate")).toInt(),0,6);
            friends_.push_back(std::move(entry));
        }
        requestSummaries(generation,key);
    });
}

void SteamFriends::fail(quint64 generation,const QString& message) {
    if(!isCurrent(generation))return;
    ++generation_;
    cancelRequests();
    const bool wasBusy=busy_;
    busy_=false;
    key_.clear();
    friendIds_.clear();
    summaryIds_.clear();
    friends_.clear();
    if(wasBusy)emit busyChanged(false);
    emit loaded({},message);
}

} // namespace xmb
