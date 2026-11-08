// Network > Wake on LAN and Network > Home Assistant (wol_client.h, homeassistant_client.h; audit/HOME-SERVICES.md).
// Pages are ordinary XMB rows; nothing here draws.
//
//   Wake on LAN            the machines in <data>/wol.json with Online/Offline (a TCP probe); Cross wakes one and
//                          reports when it answers; "Add from This Network" lists the neighbours Windows has seen
//                          (IP + MAC) and Cross adds one to wol.json
//   Home Assistant         rooms (HA areas) or kinds as folders; scenes and scripts run, lights / switches / fans /
//                          input booleans toggle, media players play/pause, sensors and climate show their state
#include "shell.h"
#include "assets.h"
#include "glyphs.h"
#include "service_icons.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace xmb {
namespace {
Entry homeRow(QString id,QString title,Action action,QString value={},QString detail={}) {
    Entry e;e.id=std::move(id);e.title=std::move(title);e.action=action;e.value=std::move(value);e.detail=std::move(detail);
    e.container=QStringLiteral("explore_plugin_full");e.texture=QStringLiteral("item_tex_plain_folder");e.source=QStringLiteral("native");
    return e;
}
Entry iconed(Entry e,const QImage& icon){if(!icon.isNull()){e.artwork=icon;e.iconSized=true;}return e;}
QString entityState(const HaEntity& e) {
    QString state=e.state;
    if(state==QLatin1String("on"))state=QObject::tr("On");
    else if(state==QLatin1String("off"))state=QObject::tr("Off");
    else if(state==QLatin1String("unavailable"))state=QObject::tr("Unavailable");
    if(!e.unit.isEmpty())state+=QStringLiteral(" ")+e.unit;
    return state;
}
}

// ---- Wake on LAN ---------------------------------------------------------------------------------------------
QVector<Entry> Shell::wolRows(const QString& kind) const {
    QVector<Entry> rows;
    if(kind==QLatin1String("add")) {
        for(const auto& c:wolCandidates_) {
            // Hyper-V / WSL virtual adapters (00:15:5D) are not machines on the network.
            if(c.mac.startsWith(QLatin1String("00:15:5D")))continue;
            bool listed=false;for(const auto& d:wolDevices_)if(d.mac==c.mac)listed=true;
            auto row=iconed(homeRow(QStringLiteral("wol:cand:")+c.mac,c.host.isEmpty()?c.mac:c.host,Action::WolAdd,c.mac,
                listed?tr("%1 - already listed").arg(c.mac):c.mac),glyphs::device(QStringLiteral("Computer")));
            if(listed)row.disabled=true;
            rows.push_back(std::move(row));
        }
        if(rows.isEmpty())rows.push_back(homeRow(QStringLiteral("wol:none"),tr("No Machines Seen Yet"),Action::Information,
            tr("Windows lists only machines it has talked to recently. Open a share or ping the machine once, then look again."),QString()));
        return rows;
    }
    for(const auto& d:wolDevices_) {
        QString detail=wolWaking_.contains(d.id)?wolWaking_.value(d.id)
            :wolOnline_.contains(d.id)?(wolOnline_.value(d.id)?tr("Online"):tr("Offline - Cross wakes it")):tr("Checking...");
        if(!d.host.isEmpty())detail+=QStringLiteral(" - ")+d.host;
        rows.push_back(iconed(homeRow(QStringLiteral("wol:dev:")+d.id,d.name,Action::WolWake,d.id,detail),glyphs::device(QStringLiteral("Computer"))));
    }
    rows.push_back(iconed(homeRow(QStringLiteral("wol:add"),tr("Add from This Network"),Action::WolAddPage,QString(),
        tr("Machines Windows has seen on this network")),glyphs::search()));
    if(!wolErrors_.isEmpty())rows.push_back(homeRow(QStringLiteral("wol:errors"),tr("Some Entries Were Skipped"),Action::Information,
        wolErrors_.join(QStringLiteral("\n")),tr("%1 entr(ies) in wol.json were not valid").arg(wolErrors_.size())));
    return rows;
}

void Shell::refreshWolPages() {
    for(auto& page:pages_) {
        if(page.wol.isEmpty())continue;
        const QString focused=page.entries.value(page.selection).id;
        page.entries=wolRows(page.wol);page.selection=0;
        for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==focused){page.selection=i;break;}
    }
}

void Shell::reloadWolDevices() {
    wolErrors_.clear();
    wolDevices_=loadWolDevices(dataDirectory_,&wolErrors_);
    for(const auto& d:wolDevices_)if(!d.host.isEmpty())wol_.probe(d);
}

bool Shell::addWolDevice(const QString& mac,const QString& host) {
    QFile in(QDir(dataDirectory_).filePath(QStringLiteral("wol.json")));
    QJsonObject root;
    if(in.size()>0 && in.size()<=64*1024 && in.open(QIODevice::ReadOnly))root=QJsonDocument::fromJson(in.readAll()).object();
    in.close();
    QJsonArray devices=root.value(QStringLiteral("devices")).toArray();
    if(devices.size()>=64)return false;
    QJsonObject device{{"name",host.isEmpty()?mac:host},{"mac",mac}};
    if(!host.isEmpty())device.insert(QStringLiteral("host"),host);
    devices.append(device);root.insert(QStringLiteral("devices"),devices);
    const QByteArray bytes=QJsonDocument(root).toJson(QJsonDocument::Indented);
    QStringList errors;
    if(parseWolDevices(bytes,&errors).size()!=devices.size())return false;   // the file must still validate
    QSaveFile out(QDir(dataDirectory_).filePath(QStringLiteral("wol.json")));
    if(!out.open(QIODevice::WriteOnly) || out.write(bytes)!=bytes.size() || !out.commit())return false;
    qInfo().noquote()<<"wol: added"<<mac<<host<<"to wol.json";
    return true;
}

// ---- Home Assistant ------------------------------------------------------------------------------------------
QVector<Entry> Shell::haRows(const QString& kind) const {
    QVector<Entry> rows;
    if(!ha_.configured() || !ha_.hasToken()) {
        rows.push_back(iconed(homeRow(QStringLiteral("ha:setup"),tr("Set Up Home Assistant"),Action::Information,
            tr("In Home Assistant open your profile, then Security, and create a long-lived access token. Put it in "
               "homeassistant.json in the data folder as {\"url\": \"http://<your HA address>\", \"token\": \"<token>\"}. "
               "On the next start the token moves into an encrypted file and the json keeps only the address."),
            ha_.configurationError().isEmpty()?tr("Needs a long-lived access token"):ha_.configurationError()),serviceIcons::homeAssistant()));
        return rows;
    }
    if(!haError_.isEmpty() && haGroups_.isEmpty()) {
        rows.push_back(homeRow(QStringLiteral("ha:error"),tr("Home Assistant Did Not Answer"),Action::Information,haError_,haError_));
        return rows;
    }
    if(kind==QLatin1String("root")) {
        for(int i=0;i<haGroups_.size();++i) {
            const auto& g=haGroups_[i];
            rows.push_back(iconed(homeRow(QStringLiteral("ha:group:")+g.id,g.name,Action::HaGroup,QString::number(i),
                tr("%n item(s)","",int(g.entities.size()))),glyphs::house()));
        }
        return rows;
    }
    const int index=kind.mid(QStringLiteral("group:").size()).toInt();
    if(index<0 || index>=haGroups_.size())return rows;
    for(const auto& e:haGroups_[index].entities) {
        // The latest state, when a refresh has replaced the group's copy.
        HaEntity live=e;
        for(const auto& s:haEntities_)if(s.id==e.id){live=s;break;}
        const bool actionable=live.domain==QLatin1String("scene") || live.domain==QLatin1String("script") ||
            live.domain==QLatin1String("light") || live.domain==QLatin1String("switch") || live.domain==QLatin1String("fan") ||
            live.domain==QLatin1String("input_boolean") || live.domain==QLatin1String("media_player") || live.domain==QLatin1String("cover");
        QString detail=live.domain==QLatin1String("scene")?tr("Scene - Cross activates it")
                      :live.domain==QLatin1String("script")?tr("Script - Cross runs it"):entityState(live);
        auto row=homeRow(QStringLiteral("ha:entity:")+live.id,live.name,actionable?Action::HaEntity:Action::Information,live.id,detail);
        if(!actionable)row.value=live.name+QStringLiteral(": ")+entityState(live);
        if(live.state==QLatin1String("unavailable"))row.disabled=true;
        const QString& d=live.domain;
        row=iconed(std::move(row),d==QLatin1String("light")?glyphs::bulb()
            :(d==QLatin1String("scene")||d==QLatin1String("script"))?glyphs::play()
            :d==QLatin1String("media_player")?glyphs::device(QStringLiteral("TV"))
            :(d==QLatin1String("switch")||d==QLatin1String("fan")||d==QLatin1String("input_boolean"))?glyphs::powerOn()
            :glyphs::more());
        rows.push_back(std::move(row));
    }
    return rows;
}

void Shell::refreshHaPages() {
    for(auto& page:pages_) {
        if(page.ha.isEmpty())continue;
        const QString focused=page.entries.value(page.selection).id;
        page.entries=haRows(page.ha);page.selection=0;
        for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==focused){page.selection=i;break;}
    }
    for(auto& column:categories_)for(auto& row:column.entries)if(row.action==Action::HaApp)row.artwork=serviceIcons::homeAssistant();
}

void Shell::initHome() {
    for(auto& column:categories_) {
        if(column.id!=QLatin1String("network"))continue;
        auto wol=iconed(homeRow(QStringLiteral("wol"),tr("Wake on LAN"),Action::WolApp,QString(),tr("Wake a computer on this network")),glyphs::powerOn());
        wol.container=QStringLiteral("explore_category_network");column.entries.push_back(std::move(wol));
        auto ha=iconed(homeRow(QStringLiteral("homeassistant"),QStringLiteral("Home Assistant"),Action::HaApp,QString(),tr("Scenes, lights and media")),
                       serviceIcons::homeAssistant());
        ha.container=QStringLiteral("explore_category_network");column.entries.push_back(std::move(ha));
    }
    connect(&wol_,&WolClient::reachable,this,[this](const QString& id,bool online){wolOnline_[id]=online;refreshWolPages();emit changed();});
    connect(&wol_,&WolClient::progress,this,[this](const QString& id,const QString& message){wolWaking_[id]=message;refreshWolPages();emit changed();});
    connect(&wol_,&WolClient::woke,this,[this](const QString& id,int seconds){
        wolWaking_.remove(id);wolOnline_[id]=true;
        for(const auto& d:wolDevices_)if(d.id==id)notify(tr("Wake on LAN"),tr("%1 is on (%2 s)").arg(d.name).arg(seconds));
        refreshWolPages();emit changed();
    });
    connect(&wol_,&WolClient::timedOut,this,[this](const QString& id){
        wolWaking_.remove(id);
        for(const auto& d:wolDevices_)if(d.id==id)notify(tr("Wake on LAN"),tr("%1 did not answer").arg(d.name));
        refreshWolPages();emit changed();
    });
    connect(&wol_,&WolClient::failed,this,[this](const QString& id,const QString& message){
        wolWaking_.remove(id);showMessage(tr("Wake on LAN"),message,MessageKind::Page);refreshWolPages();emit changed();
    });

    ha_.configure(dataDirectory_);
    if(ha_.configured())
        serviceIcons::loadHomeAssistant(dlnaNetwork_,ha_.baseUrl(),QDir(dataDirectory_).filePath("homeassistant/server-icon.png"),
                                        [this]{refreshHaPages();emit changed();});
    connect(&ha_,&HomeAssistantClient::statesReady,this,[this](quint64 request,const QVector<HaEntity>& entities){
        if(request!=haRequest_)return;
        haEntities_=entities;haError_.clear();
        haRequest_=ha_.groups(entities);
    });
    connect(&ha_,&HomeAssistantClient::groupsReady,this,[this](quint64 request,const QVector<HaGroup>& groups,bool byArea){
        if(request!=haRequest_)return;
        haRequest_=0;haGroups_=groups;loading_=false;status_.clear();
        qInfo().noquote()<<"homeassistant:"<<haEntities_.size()<<"entities in"<<groups.size()<<(byArea?"rooms":"kinds");
        refreshHaPages();emit changed();
    });
    connect(&ha_,&HomeAssistantClient::failed,this,[this](quint64 request,const QString& message){
        if(request!=haRequest_)return;
        haRequest_=0;haError_=message;loading_=false;status_.clear();refreshHaPages();emit changed();
    });
    connect(&ha_,&HomeAssistantClient::done,this,[this](quint64,bool ok,const QString& message){
        if(!ok)showMessage(QStringLiteral("Home Assistant"),message,MessageKind::Page);
        if(ha_.hasToken())haRequest_=ha_.states();   // read the new states back
        emit changed();
    });
}

bool Shell::activateHome(const Entry& entry) {
    switch(entry.action) {
    case Action::WolApp: {
        if(pages_.size()>=32)return true;
        reloadWolDevices();
        Page page;page.title=tr("Wake on LAN");page.wol=QStringLiteral("root");page.entries=wolRows(page.wol);
        pages_.push_back(std::move(page));
        return true;
    }
    case Action::WolWake:
        for(const auto& d:wolDevices_)if(d.id==entry.value) {
            if(wolOnline_.value(d.id)){notify(tr("Wake on LAN"),tr("%1 is already on").arg(d.name));return true;}
            wolWaking_[d.id]=tr("Waking...");wol_.wake(d);refreshWolPages();
        }
        return true;
    case Action::WolAddPage: {
        if(pages_.size()>=32)return true;
        QString error;wolCandidates_=discoverCandidates(&error);
        Page page;page.title=tr("Add from This Network");page.wol=QStringLiteral("add");page.entries=wolRows(page.wol);
        pages_.push_back(std::move(page));
        return true;
    }
    case Action::WolAdd:
        for(const auto& c:wolCandidates_)if(c.mac==entry.value) {
            if(addWolDevice(c.mac,c.host)) {
                notify(tr("Wake on LAN"),tr("%1 added").arg(c.host.isEmpty()?c.mac:c.host));
                if(!pages_.isEmpty() && pages_.last().wol==QLatin1String("add"))pages_.removeLast();
                reloadWolDevices();refreshWolPages();
            } else showMessage(tr("Wake on LAN"),tr("wol.json could not be updated."),MessageKind::Page);
        }
        return true;
    case Action::HaApp: {
        if(pages_.size()>=32)return true;
        Page page;page.title=QStringLiteral("Home Assistant");page.ha=QStringLiteral("root");page.entries=haRows(page.ha);
        pages_.push_back(std::move(page));
        if(ha_.configured() && ha_.hasToken()){loading_=true;status_=tr("Reading Home Assistant...");haRequest_=ha_.states();}
        return true;
    }
    case Action::HaGroup: {
        if(pages_.size()>=32)return true;
        Page page;page.title=entry.title;page.ha=QStringLiteral("group:")+entry.value;page.entries=haRows(page.ha);
        pages_.push_back(std::move(page));
        return true;
    }
    case Action::HaEntity: {
        const QString id=entry.value,domain=id.section(QLatin1Char('.'),0,0);
        if(domain==QLatin1String("scene"))ha_.activateScene(id);
        else if(domain==QLatin1String("script"))ha_.runScript(id);
        else if(domain==QLatin1String("media_player"))ha_.mediaCommand(id,QStringLiteral("media_play_pause"));
        else ha_.toggle(id);
        notify(QStringLiteral("Home Assistant"),entry.title);
        return true;
    }
    default:return false;
    }
}

}
