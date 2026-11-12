// Network > Tailscale (tailscale_client.h; audit/TAILSCALE.md). Pages are ordinary XMB rows; nothing here draws.
//
//   Tailscale (Network column)  status in the detail line: connected as <host> - <tailnet IP>, Off, Sign-in needed
//   Tailscale page              On/Off (Off asks first), Exit Node, then this tailnet's devices (online first)
//   a device                    Cross pings it over the tailnet and reports the result
//   Exit Node                   None, then every device that offers itself as an exit node
//
// The CLI is the only interface (tailscale.exe, argument arrays, bounded). Turning Tailscale off or choosing an
// exit node changes this PC's network, so those happen only on the user's explicit choice on these pages.
#include "shell.h"
#include "diagnostics.h"
#include "boot_profile.h"
#include "boot_prefetch.h"
#include <QPointer>
#include "assets.h"
#include "glyphs.h"
#include "service_icons.h"
#include <QDesktopServices>
#include <QTimer>
#include <QUrl>

namespace xmb {
namespace {
Entry tsRow(QString id,QString title,Action action,QString value={},QString detail={}) {
    Entry e;e.id=std::move(id);e.title=std::move(title);e.action=action;e.value=std::move(value);e.detail=std::move(detail);
    e.container=QStringLiteral("explore_plugin_full");e.texture=QStringLiteral("item_tex_plain_folder");e.source=QStringLiteral("native");
    return e;
}
Entry withIcon(Entry e,const QImage& icon){if(!icon.isNull()){e.artwork=icon;e.iconSized=true;}return e;}
QString firstIp(const TailscalePeer& p){return p.ips.isEmpty()?QString():p.ips.first();}
// Spotify's device kinds are the glyph vocabulary; map a tailnet node's OS onto them.
QString deviceKind(const QString& os) {
    const QString o=os.toLower();
    if(o==QLatin1String("ios") || o==QLatin1String("android"))return QStringLiteral("Smartphone");
    if(o==QLatin1String("tvos"))return QStringLiteral("TV");
    return QStringLiteral("Computer");
}
QString ago(const QDateTime& when) {
    if(!when.isValid())return {};
    const qint64 s=when.secsTo(QDateTime::currentDateTimeUtc());
    if(s<3600)return QObject::tr("last seen %1 min ago").arg(std::max<qint64>(1,s/60));
    if(s<86400)return QObject::tr("last seen %1 h ago").arg(s/3600);
    return QObject::tr("last seen %1 d ago").arg(s/86400);
}
}

QString Shell::tailscaleSummary() const {
    if(!tailscale_.installed())return tr("Not installed");
    if(!tailscaleHave_)return tailscaleError_.isEmpty()?tr("Reading status..."):tailscaleError_;
    const auto& s=tailscaleStatus_;
    if(s.needsLogin())return tr("Sign-in needed");
    if(!s.running())return tr("Off");
    QString text=tr("Connected as %1").arg(s.self.hostName);
    if(!firstIp(s.self).isEmpty())text+=QStringLiteral(" - ")+firstIp(s.self);
    if(!s.exitNodeName.isEmpty())text+=tr(" - via %1").arg(s.exitNodeName);
    return text;
}

QVector<Entry> Shell::tailscaleRows(const QString& kind) const {
    QVector<Entry> rows;
    const auto& s=tailscaleStatus_;
    if(kind==QLatin1String("exit")) {
        auto none=tsRow(QStringLiteral("ts:exit:none"),tr("None"),Action::TailscaleExitNode,QString(),tr("Use this PC's own internet connection."));
        if(s.exitNodeId.isEmpty())none.detail+=tr(" (current)");
        rows.push_back(none);
        for(const auto& p:s.peers) {
            if(!p.exitNodeOption)continue;
            auto row=withIcon(tsRow(QStringLiteral("ts:exit:")+p.id,p.hostName,Action::TailscaleExitNode,firstIp(p),
                p.online?(p.exitNode?tr("Online - current exit node"):tr("Online")):tr("Offline")),glyphs::device(deviceKind(p.os)));
            if(!p.online)row.disabled=true;
            rows.push_back(std::move(row));
        }
        return rows;
    }
    if(!tailscale_.installed()) {
        rows.push_back(tsRow(QStringLiteral("ts:missing"),tr("Tailscale Is Not Installed"),Action::Information,
            tr("Install Tailscale for Windows from tailscale.com, sign in, then come back here."),tr("tailscale.exe was not found")));
        return rows;
    }
    if(s.needsLogin()) {
        rows.push_back(withIcon(tsRow(QStringLiteral("ts:login"),tr("Sign In to Tailscale"),Action::TailscaleToggle,QStringLiteral("up"),
            tr("Opens Tailscale's sign-in in the browser.")),serviceIcons::tailscale()));
        return rows;
    }
    const bool on=s.running();
    rows.push_back(withIcon(tsRow(QStringLiteral("ts:toggle"),on?tr("Tailscale Is On"):tr("Tailscale Is Off"),Action::TailscaleToggle,
        on?QStringLiteral("down"):QStringLiteral("up"),
        on?tailscaleSummary()+(s.tailnetName.isEmpty()?QString():QStringLiteral(" - ")+s.tailnetName):tr("Cross turns it on.")),serviceIcons::tailscale()));
    if(!on)return rows;
    rows.push_back(withIcon(tsRow(QStringLiteral("ts:exit"),tr("Exit Node"),Action::TailscaleExitNodes,QString(),
        s.exitNodeName.isEmpty()?tr("None"):s.exitNodeName),glyphs::more()));
    for(const auto& p:s.peers) {
        QStringList detail;
        detail<<(p.online?tr("Online"):tr("Offline"));
        if(!p.os.isEmpty())detail<<p.os;
        if(!firstIp(p).isEmpty())detail<<firstIp(p);
        if(!p.online && p.lastSeen.isValid())detail<<ago(p.lastSeen);
        if(p.exitNode)detail<<tr("exit node");
        auto row=withIcon(tsRow(QStringLiteral("ts:peer:")+p.id,p.hostName,Action::TailscalePeer,firstIp(p),detail.join(QStringLiteral(" - "))),
                          glyphs::device(deviceKind(p.os)));
        if(!p.online)row.disabled=true;
        rows.push_back(std::move(row));
    }
    return rows;
}

void Shell::refreshTailscalePages() {
    for(auto& page:pages_) {
        if(page.tailscale.isEmpty())continue;
        const QString focused=page.entries.value(page.selection).id;
        page.entries=tailscaleRows(page.tailscale);
        page.selection=0;
        for(int i=0;i<page.entries.size();++i)if(page.entries[i].id==focused){page.selection=i;break;}
    }
    for(auto& column:categories_)for(auto& row:column.entries)if(row.action==Action::TailscaleApp)row.detail=tailscaleSummary();
}

void Shell::initTailscale() {
    xmb::bootprof::mark("tailscale start");
    // The Network column's row, after Download Management.
    for(auto& column:categories_) {
        if(column.id!=QLatin1String("network"))continue;
        auto row=withIcon(tsRow(QStringLiteral("tailscale"),QStringLiteral("Tailscale"),Action::TailscaleApp,QString(),tailscaleSummary()),
                          serviceIcons::tailscale());
        row.container=QStringLiteral("explore_category_network");
        column.entries.push_back(std::move(row));
    }
    xmb::bootprof::mark("tailscale row+icon");
    tailscaleTimer_=new QTimer(this);tailscaleTimer_->setInterval(10000);
    connect(tailscaleTimer_,&QTimer::timeout,this,[this]{
        const bool open=std::any_of(pages_.begin(),pages_.end(),[](const Page& p){return !p.tailscale.isEmpty();});
        if(open && !tailscale_.busy())tailscale_.refresh();
    });
    tailscaleTimer_->start();
    connect(&tailscale_,&TailscaleClient::statusReady,this,[this](const TailscaleStatus& status){
        tailscaleStatus_=status;tailscaleHave_=true;tailscaleError_.clear();
        qInfo().noquote()<<"tailscale:"<<status.backendState<<status.peers.size()<<"peer(s)";
        if(tailscaleLoading_){tailscaleLoading_=false;loading_=false;status_.clear();}
        refreshTailscalePages();emit changed();
    });
    connect(&tailscale_,&TailscaleClient::failed,this,[this](const QString& message){
        tailscaleError_=message;
        if(tailscaleLoading_) {
            tailscaleLoading_=false;loading_=false;status_.clear();
            showMessage(QStringLiteral("Tailscale"),message,MessageKind::Page);
        }
        refreshTailscalePages();emit changed();
    });
    connect(&tailscale_,&TailscaleClient::commandDone,this,[this](const QString& what,bool ok,const QString& message){
        loading_=false;status_.clear();
        if(what==QLatin1String("ping")) {
            notify(QStringLiteral("Tailscale"),ok?message:tr("No answer: %1").arg(message));
        } else if(what==QLatin1String("up") && message.startsWith(QLatin1String("https://"))) {
            // Sign-in: Tailscale hands back its own login page.
            QDesktopServices::openUrl(QUrl(message));
            notify(QStringLiteral("Tailscale"),tr("Finish signing in in the browser."));
        } else if(!ok) {
            showMessage(QStringLiteral("Tailscale"),message,MessageKind::Page);
        } else {
            notify(QStringLiteral("Tailscale"),what==QLatin1String("down")?tr("Turned off"):what==QLatin1String("up")?tr("Turned on"):tr("Exit node changed"));
        }
        tailscale_.refresh();
        emit changed();
    });
    xmb::bootprof::mark("tailscale connects");
    // D6: no network in safe mode. ZF4: the status read starts a tailscale process (~40 ms of the GUI thread); nothing of it is drawn
    // by the first frame, so it starts right after it (the answer was always asynchronous).
    if(!diagnostics::safeMode())
        xmb::boot::afterFirstFrame([self=QPointer<Shell>(this)]{if(self && self->tailscale_.installed())self->tailscale_.refresh();});
    xmb::bootprof::mark("tailscale installed+refresh");
}

bool Shell::activateTailscale(const Entry& entry) {
    switch(entry.action) {
    case Action::TailscaleApp: {
        if(pages_.size()>=32)return true;
        Page page;page.title=QStringLiteral("Tailscale");page.tailscale=QStringLiteral("root");page.entries=tailscaleRows(page.tailscale);
        pages_.push_back(std::move(page));
        if(tailscale_.installed()){tailscaleLoading_=!tailscaleHave_;if(tailscaleLoading_){loading_=true;status_=tr("Reading Tailscale status...");}tailscale_.refresh();}
        return true;
    }
    case Action::TailscaleToggle:
        if(tailscale_.busy())return true;
        if(entry.value==QLatin1String("down")) {
            showConfirm(tr("Do you want to turn Tailscale off on this PC? Devices on your tailnet will not reach it until it is turned on again."),
                        QString(),[this]{loading_=true;status_=tr("Turning Tailscale off...");tailscale_.setConnected(false);emit changed();});
        } else {loading_=true;status_=tr("Turning Tailscale on...");tailscale_.setConnected(true);}
        return true;
    case Action::TailscaleExitNodes: {
        if(pages_.size()>=32)return true;
        Page page;page.title=tr("Exit Node");page.tailscale=QStringLiteral("exit");page.entries=tailscaleRows(page.tailscale);
        pages_.push_back(std::move(page));
        return true;
    }
    case Action::TailscaleExitNode:
        if(tailscale_.busy())return true;
        loading_=true;status_=tr("Changing the exit node...");
        tailscale_.setExitNode(entry.value);
        if(!pages_.isEmpty() && pages_.last().tailscale==QLatin1String("exit"))pages_.removeLast();
        return true;
    case Action::TailscalePeer:
        if(entry.value.isEmpty())return true;
        status_=tr("Pinging %1...").arg(entry.title);
        tailscale_.ping(entry.value);
        return true;
    default:return false;
    }
}

}
