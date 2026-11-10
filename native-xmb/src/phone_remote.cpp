#include "phone_remote.h"
#include <QHostAddress>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace xmb {
namespace {
const QByteArray page=R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>XMB remote</title><style>body{background:#172234;color:white;font:20px sans-serif;text-align:center}button,input{font-size:22px;padding:16px;margin:5px}button{min-width:90px;touch-action:manipulation}</style></head><body><h1>XMB remote</h1><div><button onclick="send('UP')">Up</button></div><div><button onclick="send('LEFT')">Left</button><button onclick="send('DOWN')">Down</button><button onclick="send('RIGHT')">Right</button></div><div><button onclick="send('CROSS')">Cross</button><button onclick="send('CIRCLE')">Circle</button><button onclick="send('TRIANGLE')">Triangle</button><button onclick="send('PS')">PS</button></div><form onsubmit="text();return false"><input id="text" placeholder="Text"><button>Send text</button></form><p id="status"></p><script>const code=new URLSearchParams(location.search).get('code');async function request(path){try{let r=await fetch(path+'&code='+encodeURIComponent(code),{method:'POST'});document.getElementById('status').textContent=await r.text()}catch(e){document.getElementById('status').textContent='Remote unavailable'}}function send(b){request('/button?value='+encodeURIComponent(b))}function text(){request('/text?value='+encodeURIComponent(document.getElementById('text').value))}</script></body></html>)HTML";
void respond(QTcpSocket* socket,int status,const QByteArray& type,const QByteArray& body) {
    const QByteArray reason=status==200?"OK":status==403?"Forbidden":status==404?"Not Found":"Bad Request";
    socket->write("HTTP/1.1 "+QByteArray::number(status)+" "+reason+"\r\nContent-Type: "+type+"\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n"+body);
    socket->disconnectFromHost();
}
}
PhoneRemote::PhoneRemote(QObject* parent):QObject(parent) {
    connect(&server_,&QTcpServer::newConnection,this,[this]{
        while(auto* socket=server_.nextPendingConnection()) {
            socket->setParent(this);
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QTimer::singleShot(5000,socket,[socket]{socket->abort();socket->deleteLater();});
            connect(socket,&QTcpSocket::readyRead,this,[this,socket]{
                if(socket->bytesAvailable()>8192){respond(socket,400,"text/plain","Request too large");return;}
                const QByteArray data=socket->peek(8193);
                if(!data.contains("\r\n\r\n"))return;
                socket->readAll();
                const auto line=data.left(data.indexOf("\r\n")).split(' ');
                if(line.size()!=3){respond(socket,400,"text/plain","Bad request");return;}
                const QUrl target=QUrl::fromEncoded(line[1],QUrl::StrictMode);
                const QUrlQuery query(target);
                if(!target.isValid() || query.queryItemValue(QStringLiteral("code"))!=code_){respond(socket,403,"text/plain","Forbidden");return;}
                if(line[0]=="GET" && target.path()==QStringLiteral("/")){respond(socket,200,"text/html; charset=utf-8",page);return;}
                if(line[0]=="POST" && target.path()==QStringLiteral("/text")) {
                    const QString value=query.queryItemValue(QStringLiteral("value"));
                    if(value.isEmpty()){respond(socket,400,"text/plain","Empty text");return;}
                    emit textRequested(value);
                    respond(socket,200,"text/plain","Sent");return;
                }
                if(line[0]=="POST" && target.path()==QStringLiteral("/button")) {
                    const QString value=query.queryItemValue(QStringLiteral("value"));
                    static const QHash<QString,int> keys{{QStringLiteral("LEFT"),Qt::Key_Left},{QStringLiteral("RIGHT"),Qt::Key_Right},{QStringLiteral("UP"),Qt::Key_Up},{QStringLiteral("DOWN"),Qt::Key_Down},{QStringLiteral("CROSS"),Qt::Key_Return},{QStringLiteral("CIRCLE"),Qt::Key_Escape},{QStringLiteral("TRIANGLE"),Qt::Key_T}};
                    if(value==QStringLiteral("PS"))emit psRequested();
                    else if(keys.contains(value))emit keyRequested(keys.value(value));
                    else {respond(socket,400,"text/plain","Unknown button");return;}
                    respond(socket,200,"text/plain","Sent");return;
                }
                respond(socket,404,"text/plain","Not found");
            });
        }
    });
}
bool PhoneRemote::setEnabled(bool enabled) {
    if(!enabled){server_.close();code_.clear();for(auto* socket:findChildren<QTcpSocket*>())socket->abort();return true;}
    if(isEnabled())return true;
    code_=QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000000),6,10,QLatin1Char('0'));
    // LAN access is opt-in (XMB_PHONE_REMOTE_LAN=1): localhost is the safe default so the input-injection endpoint
    // is not exposed to the network without an explicit choice. The pairing code still gates every request.
    const bool lan=qEnvironmentVariableIntValue("XMB_PHONE_REMOTE_LAN")==1;
    if(!server_.listen(lan?QHostAddress::Any:QHostAddress::LocalHost,0)){code_.clear();return false;}
    return true;
}
QUrl PhoneRemote::url() const {
    if(!isEnabled())return {};
    // When bound to any interface, name the machine's first non-loopback IPv4 address so a phone can reach it;
    // otherwise localhost. The ?code= pairing code is in the URL.
    QString host=QStringLiteral("127.0.0.1");
    if(server_.serverAddress()==QHostAddress::Any) {
        for(const auto& addr:QNetworkInterface::allAddresses())
            if(addr.protocol()==QAbstractSocket::IPv4Protocol && !addr.isLoopback()){host=addr.toString();break;}
    }
    return QUrl(QStringLiteral("http://%1:%2/?code=%3").arg(host).arg(server_.serverPort()).arg(code_));
}
}
