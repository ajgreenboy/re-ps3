#include "steam_friends.h"

#include "steam_owned.h"

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
    nextSummary_=0;

    const QString id=steamId.trimmed();
    if(!validSteamId(id)) {
        if(busy_){busy_=false;emit busyChanged(false);}
        emit loaded({},id.isEmpty()?QStringLiteral("No Steam account is selected. Sign in to Steam, or set XMB_STEAM_ID to your numeric Steam ID and restart XMB."):
            QStringLiteral("Steam ID must be a nonzero numeric 64-bit ID."));
        return;
    }
    const QString key=SteamOwned::apiKey();
    if(key.isEmpty()) {
        if(busy_){busy_=false;emit busyChanged(false);}
        emit loaded({},QStringLiteral("Steam friends need a Steam Web API key. Set STEAM_WEB_API_KEY or write the key to the steam_api_key file and restart XMB."));
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

void SteamFriends::requestFriendList(quint64 generation,const QString& steamId,const QString& key) {
    QUrl url(QStringLiteral("https://api.steampowered.com/ISteamUser/GetFriendList/v1/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"),key);
    query.addQueryItem(QStringLiteral("steamid"),steamId);
    query.addQueryItem(QStringLiteral("relationship"),QStringLiteral("friend"));
    url.setQuery(query);

    QNetworkRequest request(url);
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
        if(status==401) {
            fail(generation,QStringLiteral("This Steam friends list is private. Set the profile's friends list to public, then refresh."));
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
        std::sort(friends_.begin(),friends_.end(),[](const SteamFriend& left,const SteamFriend& right){
            const int leftBucket=sortBucket(left),rightBucket=sortBucket(right);
            if(leftBucket!=rightBucket)return leftBucket<rightBucket;
            const int nameOrder=QString::compare(left.name,right.name,Qt::CaseInsensitive);
            if(nameOrder!=0)return nameOrder<0;
            return left.steamId<right.steamId;
        });
        busy_=false;
        key_.clear();
        emit busyChanged(false);
        emit loaded(friends_,{});
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
    query.addQueryItem(QStringLiteral("key"),key);
    query.addQueryItem(QStringLiteral("steamids"),ids.join(','));
    url.setQuery(query);

    QNetworkRequest request(url);
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
